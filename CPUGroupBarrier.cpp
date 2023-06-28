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
		mCompletionFunction(CompletionFunction()), mBarrierRed(std::barrier<CompletionFunction>(blockSize.x * blockSize.y * blockSize.z, mCompletionFunction)), mBarrierBlack(std::barrier<CompletionFunction>(blockSize.x * blockSize.y * blockSize.z, mCompletionFunction))
	{
		mThreadGroupRed.reserve(blockSize.x * blockSize.y * blockSize.z);
		mThreadGroupBlack.reserve(blockSize.x * blockSize.y * blockSize.z);
	}

	struct CompletionFunction
	{
		CompletionFunction()
		{
		}

		void operator()() noexcept
		{
		}
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
			mBarrierRed.arrive_and_wait();
		}
		else
		{
			mBarrierBlack.arrive_and_wait();
		}
	}

	void DispatchGroup(Vector3UInt groupIndex)
	{
		std::vector<std::thread>& currentThreadGroup = mUseRed ? mThreadGroupRed : mThreadGroupBlack;
		std::vector<std::thread>& otherThreadGroup = mUseRed ? mThreadGroupBlack : mThreadGroupRed;

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

	CompletionFunction mCompletionFunction;

	std::barrier<CompletionFunction> mBarrierRed;
	std::barrier<CompletionFunction> mBarrierBlack;

	bool mUseRed = true;
	std::vector<std::thread> mThreadGroupRed;
	std::vector<std::thread> mThreadGroupBlack;

	std::function<void(Vector3UInt, Vector3UInt, bool, CPUDispatcher*)> mKernel;
};

#define BLOCK_BARRIER() dispatcher->GroupSync(useRed)
#define Compute_Kernel(name) void ComputeKernel_##name(Vector3UInt groupIndex, Vector3UInt threadIndex, bool useRed, CPUDispatcher* dispatcher)

Compute_Kernel(Main)
{
	printf("[ThreadDispatcher] Group %u Thread %u Initialized. \n", groupIndex.x, threadIndex.x);

	BLOCK_BARRIER();

	for (uint taskIndex = 0; taskIndex < 2; taskIndex++)
	{
		printf("[ThreadDispatcher] Group %u Thread %u Task %u Done. \n", groupIndex.x, threadIndex.x, taskIndex);
		BLOCK_BARRIER();
	}

	printf("[ThreadDispatcher] Group %u Thread %u Finalized. \n", groupIndex.x, threadIndex.x);
}

int main()
{
	CPUDispatcher dispatcher(Vector3UInt(8, 1, 1), ComputeKernel_Main);
	dispatcher.Dispatch(Vector3UInt(2, 1, 1));

	return 0;
}