#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <functional>

typedef uint32_t uint;

struct Vector3UInt
{
	Vector3UInt()
		: x(0), y(0), z(0)
	{
	}

	Vector3UInt(uint inX, uint inY, uint inZ)
		: x(inX), y(inY), z(inZ)
	{
	}

	uint x;
	uint y;
	uint z;
};

struct CPUDispatcher
{
	CPUDispatcher(Vector3UInt blockSize, std::function<void(Vector3UInt, Vector3UInt, bool, CPUDispatcher*)> kernel)
		: mBlockSize(blockSize), mKernel(kernel),
		mRedCompletionFunction(CompletionFunction(true, this)), 
		mBlackCompletionFunction(CompletionFunction(false, this))
	{
		const uint groupThreadCount = blockSize.x * blockSize.y * blockSize.z;
		mRedAliveThreadCount = groupThreadCount;
		mBlackAliveThreadCount = groupThreadCount;

		mRedGroupThreadCount = groupThreadCount;
		mBlackGroupThreadCount = groupThreadCount;

		mRedBarriers.reserve(groupThreadCount);
		mBlackBarriers.reserve(groupThreadCount);

		for (int i = 1; i <= (groupThreadCount); i++)
		{
			mRedBarriers.push_back(std::make_unique<std::barrier<CompletionFunction>>(i, mRedCompletionFunction));
			mBlackBarriers.push_back(std::make_unique<std::barrier<CompletionFunction>>(i, mBlackCompletionFunction));
		}

		mThreadGroupRed.reserve(groupThreadCount);
		mThreadGroupBlack.reserve(groupThreadCount);
	}

	struct CompletionFunction
	{
		CompletionFunction(bool isRed, CPUDispatcher* dispatcher)
			: mIsRed(isRed), mpDispatcher(dispatcher)
		{
		}

		void operator()() noexcept
		{
			if (mIsRed)
			{
				mpDispatcher->mRedGroupThreadCount = mpDispatcher->mRedAliveThreadCount;
			}
			else
			{
				mpDispatcher->mBlackGroupThreadCount = mpDispatcher->mBlackAliveThreadCount;
			}
		}

		bool mIsRed;
		CPUDispatcher* mpDispatcher;
	};

	void Dispatch(Vector3UInt groupSize)
	{
		for (uint gridX = 0; gridX < groupSize.x; gridX++)
		{
			for (uint gridY = 0; gridY < groupSize.y; gridY++)
			{
				for (uint gridZ = 0; gridZ < groupSize.z; gridZ++)
				{
					DispatchGroup(Vector3UInt(gridX, gridY, gridZ));
					mUseRed = !mUseRed;
				}
			}
		}

		for (uint32_t i = 0; i < mThreadGroupRed.size(); i++)
		{
			mThreadGroupRed[i].join();
		}
		mThreadGroupRed.clear();

		for (uint32_t i = 0; i < mThreadGroupBlack.size(); i++)
		{
			mThreadGroupBlack[i].join();
		}
		mThreadGroupBlack.clear();
	}

	void GroupSync(bool useRed)
	{
		if (useRed)
		{
			mRedBarriers[mRedGroupThreadCount - 1]->arrive_and_wait();
		}
		else
		{
			mBlackBarriers[mBlackGroupThreadCount - 1]->arrive_and_wait();
		}
	}

	void DispatchGroup(Vector3UInt groupIndex)
	{
		std::vector<std::thread>& currentThreadGroup = mUseRed ? mThreadGroupRed : mThreadGroupBlack;
		std::vector<std::thread>& otherThreadGroup = mUseRed ? mThreadGroupBlack : mThreadGroupRed;

		const uint groupThreadCount = mBlockSize.x * mBlockSize.y * mBlockSize.z;
		
		if (mUseRed)
		{
			mRedAliveThreadCount = groupThreadCount;
			mRedGroupThreadCount = groupThreadCount;
		}
		else
		{
			mBlackAliveThreadCount = groupThreadCount;
			mBlackGroupThreadCount = groupThreadCount;
		}

		for (uint32_t x = 0; x < mBlockSize.x; x++)
		{
			for (uint32_t y = 0; y < mBlockSize.y; y++)
			{
				for (uint32_t z = 0; z < mBlockSize.z; z++)
				{
					currentThreadGroup.emplace_back(mKernel, groupIndex, Vector3UInt(x, y, z), mUseRed, this);
				}
			}
		}

		for (uint32_t i = 0; i < otherThreadGroup.size(); i++)
		{
			otherThreadGroup[i].join();
		}
		otherThreadGroup.clear();
	}

	Vector3UInt mBlockSize;
	
	std::function<void(Vector3UInt, Vector3UInt, bool, CPUDispatcher*)> mKernel;

	CompletionFunction mRedCompletionFunction;
	CompletionFunction mBlackCompletionFunction;

	std::vector<std::unique_ptr<std::barrier<CompletionFunction>>> mRedBarriers;
	std::vector<std::unique_ptr<std::barrier<CompletionFunction>>> mBlackBarriers;

	bool mUseRed = true;

	std::vector<std::thread> mThreadGroupRed;
	std::vector<std::thread> mThreadGroupBlack;

	std::atomic_uint mRedAliveThreadCount;
	std::atomic_uint mBlackAliveThreadCount;

	uint mRedGroupThreadCount;
	uint mBlackGroupThreadCount;
};

#define BLOCK_BARRIER() dispatcher->GroupSync(isRed)
#define Compute_Kernel_Begin(name)                                                                                    \
	void ComputeKernel_##name(Vector3UInt groupIndex, Vector3UInt threadIndex, bool isRed, CPUDispatcher* dispatcher) \
	{                                                                                                                 \
		KernelScopeGuard scopeGuard(isRed, dispatcher);
#define Compute_Kernel_End }
struct KernelScopeGuard
{
	KernelScopeGuard(bool isRed, CPUDispatcher* dispatcher)
		: mIsRed(isRed), mpDispatcher(dispatcher)
	{
	}

	~KernelScopeGuard()
	{
		if (mIsRed)
		{
			mpDispatcher->mRedBarriers[mpDispatcher->mRedGroupThreadCount - 1]->arrive_and_drop();
			mpDispatcher->mRedAliveThreadCount.fetch_sub(1);
		}
		else
		{
			mpDispatcher->mBlackBarriers[mpDispatcher->mBlackGroupThreadCount - 1]->arrive_and_drop();
			mpDispatcher->mBlackAliveThreadCount.fetch_sub(1);
		}
	}

	bool mIsRed;
	CPUDispatcher* mpDispatcher;
};

Compute_Kernel_Begin(Main)
{
	if (threadIndex.x >= 4)
	{
		return;
	}

	printf("[ThreadDispatcher] Group %u Thread %u Initialized. \n", groupIndex.x, threadIndex.x);
	
	BLOCK_BARRIER();

	for (uint taskIndex = 0; taskIndex < 2; taskIndex++)
	{
		printf("[ThreadDispatcher] Group %u Thread %u Task %u Done. \n", groupIndex.x, threadIndex.x, taskIndex);
		BLOCK_BARRIER();
	}

	printf("[ThreadDispatcher] Group %u Thread %u Finalized. \n", groupIndex.x, threadIndex.x);
}
Compute_Kernel_End

int main()
{
	CPUDispatcher dispatcher(Vector3UInt(8, 1, 1), ComputeKernel_Main);
	dispatcher.Dispatch(Vector3UInt(2, 1, 1));

	return 0;
}