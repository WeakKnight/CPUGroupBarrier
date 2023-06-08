#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_map>

struct CPUThreadDispatcher
{
	CPUThreadDispatcher()
	{
		mCompletionFunction.dispatcher = this;
		mThreads.reserve(256llu);
	}

	struct GroupContext
	{
		GroupContext(CPUThreadDispatcher* dispatcher)
			: mpDispatcher(dispatcher)
		{
		}

		~GroupContext()
		{
			mpDispatcher->mAliveThreadCount--;
			mpDispatcher->GetBarrier(mpDispatcher->mCurrentBarrierSize)->arrive_and_drop();
		}

		CPUThreadDispatcher* mpDispatcher;
	};

	struct CompletionFunction
	{
		void operator()() noexcept
		{
			dispatcher->mCurrentBarrierSize = dispatcher->mAliveThreadCount;
		}

		CPUThreadDispatcher* dispatcher;
	};

	std::barrier<CompletionFunction>* GetBarrier(uint32_t num)
	{
		auto iter = mBarriers.find(num);
		if (iter == mBarriers.cend())
		{
			auto result = std::make_shared<std::barrier<CompletionFunction>>(mAliveThreadCount, mCompletionFunction);
			mBarriers[num] = result;
			return result.get();
		}
		else
		{
			return iter->second.get();
		}
	}

	void GroupBlock()
	{
		GetBarrier(mCurrentBarrierSize)->arrive_and_wait();
	}

	void Dispatch(uint32_t blockWidth, uint32_t blockHeight, uint32_t blockDepth)
	{
		mCurrentBarrierSize = blockWidth * blockHeight * blockDepth;
		mAliveThreadCount = mCurrentBarrierSize;

		auto work = [&](uint32_t x, uint32_t y, uint32_t z) {
			GroupContext groupContext(this);

			printf("Thread %u, %u, %u Initialized. \n", x, y, z);

			if (x == 0)
			{
				return;
			}

			GroupBlock();

			printf("Thread %u, %u, %u Working. \n", x, y, z);

			GroupBlock();

			printf("Thread %u, %u, %u Done. \n", x, y, z);
		};

		for (uint32_t x = 0; x < blockWidth; x++)
		{
			for (uint32_t y = 0; y < blockHeight; y++)
			{
				for (uint32_t z = 0; z < blockDepth; z++)
				{
					mThreads.emplace_back(work, x, y, z);
				}
			}
		}
	}

	uint32_t mCurrentBarrierSize;
	std::unordered_map<uint32_t, std::shared_ptr<std::barrier<CompletionFunction>>> mBarriers;
	CompletionFunction mCompletionFunction;
	std::atomic_uint32_t mAliveThreadCount;
	std::vector<std::jthread> mThreads;
};

struct CPUCoroutineDispatcher
{
};

int main()
{
	CPUThreadDispatcher dispatcher;
	dispatcher.Dispatch(8, 1, 1);

	return 0;
}