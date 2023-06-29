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

struct CPUTimer
{
	void Start()
	{
		mStartTime = std::chrono::system_clock::now();
		mCapturing = true;
	}

	void Stop()
	{
		mEndTime = std::chrono::system_clock::now();
		mCapturing = false;
	}

	// in microseconds
	float GetElapsedTime() const
	{
		std::chrono::time_point<std::chrono::system_clock> endTime;
		if (mCapturing)
		{
			endTime = std::chrono::system_clock::now();
		}
		else
		{
			endTime = mEndTime;
		}

		return (float)std::chrono::duration_cast<std::chrono::milliseconds>(endTime - mStartTime).count();
	}

	void Print(const std::string& name)
	{
		printf("%s takes %f ms\n", name.c_str(), GetElapsedTime());
	}

	std::chrono::time_point<std::chrono::system_clock> mStartTime;
	std::chrono::time_point<std::chrono::system_clock> mEndTime;
	bool mCapturing = false;
};

struct CPUDispatcher
{
	CPUDispatcher(Vector3UInt blockSize, std::function<void(Vector3UInt, Vector3UInt, bool, CPUDispatcher*)> kernel)
		: mBlockSize(blockSize), mKernel(kernel), mRedCompletionFunction(CompletionFunction(true, this)), mBlackCompletionFunction(CompletionFunction(false, this))
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

	void SetSharedMemorySize(uint byteSize)
	{
		if (mRedSharedMemory.capacity() < byteSize)
		{
			mRedSharedMemory.reserve(byteSize);
		}

		if (mBlackSharedMemory.capacity() < byteSize)
		{
			mBlackSharedMemory.reserve(byteSize);
		}
	}

	void* GetSharedMemory(bool isRed)
	{
		if (isRed)
		{
			return mRedSharedMemory.data();
		}
		else
		{
			return mBlackSharedMemory.data();
		}
	}

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
					currentThreadGroup.emplace_back(mKernel, groupIndex, Vector3UInt(mBlockSize.x * groupIndex.x + x, mBlockSize.y * groupIndex.y + y, mBlockSize.z * groupIndex.z + z), mUseRed, this);
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

	std::vector<uint8_t> mRedSharedMemory;
	std::vector<uint8_t> mBlackSharedMemory;

	uint mRedGroupThreadCount;
	uint mBlackGroupThreadCount;
};

#define GROUP_BARRIER() dispatcher->GroupSync(isRed)
#define Compute_Kernel_Begin(name)                                                                                    \
	void ComputeKernel_##name(Vector3UInt groupIndex, Vector3UInt threadIndex, bool isRed, CPUDispatcher* dispatcher) \
	{                                                                                                                 \
		KernelScopeGuard scopeGuard(isRed, dispatcher);
#define Compute_Kernel_End }

#define SHARED_MEM(type) (type*)dispatcher->GetSharedMemory(isRed)

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

Compute_Kernel_Begin(ParallelReduction)
{
	uint* sharedData = SHARED_MEM(uint);

	sharedData[threadIndex.x] = threadIndex.x;

	GROUP_BARRIER();

	for (uint stride = 512 / 2; stride > 0; stride /= 2)
	{
		if (threadIndex.x < stride)
		{
			sharedData[threadIndex.x] += sharedData[threadIndex.x + stride];
		}

		GROUP_BARRIER();
	}

	if (threadIndex.x == 0)
	{
		printf("Final Result: %u\n", sharedData[0]);
	}
}
Compute_Kernel_End

int main()
{
	CPUDispatcher dispatcher(Vector3UInt(512, 1, 1), ComputeKernel_ParallelReduction);
	dispatcher.SetSharedMemorySize(512 * sizeof(uint));
	CPUTimer timer;
	timer.Start();
	dispatcher.Dispatch(Vector3UInt(1, 1, 1));
	timer.Print("Parallel Sum");

	timer.Start();
	int res = 0;
	for (int i = 0; i < 512; i++)
	{
		res += i;
	}
	printf("Final Result: %u\n", res);
	timer.Print("Serial Sum");
	
	return 0;
}