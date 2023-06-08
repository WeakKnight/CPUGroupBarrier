#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

struct CPUThreadDispatcher
{
	CPUThreadDispatcher(uint32_t inWidth, uint32_t inHeight, uint32_t inDepth)
		: mWidth(inWidth), mHeight(inHeight), mDepth(inDepth)
	{
		mAliveThreadCount = inWidth * inHeight * inDepth;

		mCompletionFunction.dispatcher = this;
		mBarriers.push_back(std::make_shared<std::barrier<CompletionFunction>>(inWidth * inHeight * inDepth, mCompletionFunction));
		mThreads.reserve((size_t)inWidth * (size_t)inHeight * (size_t)inDepth);
	}

	struct GroupContext
	{
		GroupContext(CPUThreadDispatcher* dispatcher)
			:mpDispatcher(dispatcher)
		{
		}

		~GroupContext()
		{
			mpDispatcher->mAliveThreadCount--;
			mpDispatcher->mBarriers.back()->arrive_and_drop();
		}

		CPUThreadDispatcher* mpDispatcher;
	};

	struct CompletionFunction
	{
		void operator()() noexcept
		{
			dispatcher->mBarriers.push_back(std::make_shared<std::barrier<CompletionFunction>>(dispatcher->mAliveThreadCount, dispatcher->mCompletionFunction));
		}

		CPUThreadDispatcher* dispatcher;
	};

	void GroupBlock()
	{
		mBarriers.back()->arrive_and_wait();
	}

	void Dispatch()
	{
		auto work = [&](uint32_t x, uint32_t y, uint32_t z)
		{
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

		for (uint32_t x = 0; x < mWidth; x++)
		{
			for (uint32_t y = 0; y < mHeight; y++)
			{
				for (uint32_t z = 0; z < mDepth; z++)
				{
					mThreads.emplace_back(work, x, y, z);
				}
			}
		}
	}

	uint32_t mWidth;
	uint32_t mHeight;
	uint32_t mDepth;
	std::vector<std::shared_ptr<std::barrier<CompletionFunction>>> mBarriers;
	CompletionFunction mCompletionFunction;
	std::atomic_uint32_t mAliveThreadCount;
	std::vector<std::jthread> mThreads;
};

struct CPUCoroutineDispatcher
{
};

int main()
{
	CPUThreadDispatcher dispatcher(8, 1, 1);
	dispatcher.Dispatch();

	return 0;
}