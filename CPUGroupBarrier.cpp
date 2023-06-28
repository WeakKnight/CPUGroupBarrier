#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <coroutine>
#include <optional>

#include <windows.h>

#define BLOCK_WIDTH 8
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

struct CPUDispatcher
{
	CPUDispatcher()
		: mCompletionFunction(CompletionFunction()), mBarrierRed(std::barrier<CompletionFunction>(BLOCK_WIDTH, mCompletionFunction)), mBarrierBlack(std::barrier<CompletionFunction>(BLOCK_WIDTH, mCompletionFunction))
	{
		mThreadGroupRed.reserve(BLOCK_WIDTH);
		mThreadGroupBlack.reserve(BLOCK_WIDTH);
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

	void Dispatch(uint32_t groupWidth)
	{
		for (int i = 0; i < groupWidth; i++)
		{
			DispatchGroup(i);
			mUseRed = !mUseRed;
		}

		for (uint32_t x = 0; x < mThreadGroupRed.size(); x++)
		{
			mThreadGroupRed[x].join();
		}
		mThreadGroupRed.clear();

		for (uint32_t x = 0; x < mThreadGroupBlack.size(); x++)
		{
			mThreadGroupBlack[x].join();
		}
		mThreadGroupBlack.clear();
	}

	void DispatchGroup(uint32_t gIndex)
	{
		auto work = [&](uint32_t groupIndex, uint32_t threadIndex, bool useRed) {
			printf("[ThreadDispatcher] Group %u Thread %u Initialized. \n", groupIndex, threadIndex);

			if (useRed)
			{
				mBarrierRed.arrive_and_wait();
			}
			else
			{
				mBarrierBlack.arrive_and_wait();
			}

			printf("[ThreadDispatcher] Group %u Thread %u Task A Done. \n", groupIndex, threadIndex);

			if (useRed)
			{
				mBarrierRed.arrive_and_wait();
			}
			else
			{
				mBarrierBlack.arrive_and_wait();
			}

			printf("[ThreadDispatcher] Group %u Thread %u Task B Done. \n", groupIndex, threadIndex);

			if (useRed)
			{
				mBarrierRed.arrive_and_wait();
			}
			else
			{
				mBarrierBlack.arrive_and_wait();
			}

			printf("[ThreadDispatcher] Group %u Thread %u Finalized. \n", groupIndex, threadIndex);
		};

		std::vector<std::thread>& currentThreadGroup = mUseRed ? mThreadGroupRed : mThreadGroupBlack;
		std::vector<std::thread>& otherThreadGroup = mUseRed ? mThreadGroupBlack : mThreadGroupRed;

		for (uint32_t x = 0; x < BLOCK_WIDTH; x++)
		{
			currentThreadGroup.emplace_back(work, gIndex, x, mUseRed);
		}

		for (uint32_t x = 0; x < otherThreadGroup.size(); x++)
		{
			otherThreadGroup[x].join();
		}
		otherThreadGroup.clear();
	}

	CompletionFunction mCompletionFunction;

	std::barrier<CompletionFunction> mBarrierRed;
	std::barrier<CompletionFunction> mBarrierBlack;

	bool mUseRed = true;
	std::vector<std::thread> mThreadGroupRed;
	std::vector<std::thread> mThreadGroupBlack;
};

int main()
{
	CPUDispatcher dispatcher;
	dispatcher.Dispatch(4);

	return 0;
}