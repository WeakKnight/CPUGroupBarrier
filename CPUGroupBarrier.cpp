#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <coroutine>

#include <windows.h>

#define BLOCK_WIDTH 8
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

struct ThreadDispatcher
{
	ThreadDispatcher()
	{
		mCurrentBarrierSize = 0;
		mCompletionFunction.dispatcher = this;
		mThreads.reserve(BLOCK_WIDTH);
	}

	struct GroupContext
	{
		GroupContext(ThreadDispatcher* dispatcher)
			: mpDispatcher(dispatcher)
		{
		}

		~GroupContext()
		{
			mpDispatcher->mAliveThreadCount--;
			mpDispatcher->GetBarrier(mpDispatcher->mCurrentBarrierSize)->arrive_and_drop();
		}

		ThreadDispatcher* mpDispatcher;
	};

	struct CompletionFunction
	{
		void operator()() noexcept
		{
			dispatcher->mCurrentBarrierSize = dispatcher->mAliveThreadCount;
		}

		ThreadDispatcher* dispatcher;
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

	void Dispatch()
	{
		mCurrentBarrierSize = BLOCK_WIDTH;
		mAliveThreadCount = mCurrentBarrierSize;

		auto work = [&](uint32_t threadIndex) {
			GroupContext groupContext(this);

			printf("[ThreadDispatcher] Thread %u Initialized. \n", threadIndex);

			GroupBlock();

			printf("[ThreadDispatcher] Thread %u Task A Done. \n", threadIndex);

			GroupBlock();

			printf("[ThreadDispatcher] Thread %u Task B Done. \n", threadIndex);

			GroupBlock();

			printf("[ThreadDispatcher] Thread %u Finalized. \n", threadIndex);
		};

		for (uint32_t x = 0; x < BLOCK_WIDTH; x++)
		{
			mThreads.emplace_back(work, x);
		}
	}

	uint32_t mCurrentBarrierSize;
	std::unordered_map<uint32_t, std::shared_ptr<std::barrier<CompletionFunction>>> mBarriers;
	CompletionFunction mCompletionFunction;
	std::atomic_uint32_t mAliveThreadCount;
	std::vector<std::jthread> mThreads;
};

#define KERNEL_BEGIN                                                          \
	std::unordered_map<uint16_t, uint16_t> completeThreadNums;                \
	auto IncrementCompleteThreadNum = [&completeThreadNums](int lineNumber) { \
		auto iter = completeThreadNums.find(lineNumber);                      \
		if (iter == completeThreadNums.cend())                                \
		{                                                                     \
			completeThreadNums[lineNumber] = 1;                               \
			return uint16_t(1);                                               \
		}                                                                     \
		else                                                                  \
		{                                                                     \
			iter->second++;                                                   \
			return iter->second;                                              \
		}                                                                     \
	};                                                                        \
	int state = 0;                                                            \
	for (uint32_t threadIndex = 0; threadIndex < BLOCK_WIDTH; threadIndex++)  \
	{                                                                         \
		switch (state)                                                        \
		{                                                                     \
			case 0:

#define GROUP_BARRIER                                        \
	if (IncrementCompleteThreadNum(__LINE__) != BLOCK_WIDTH) \
	{                                                        \
		continue;                                            \
	}                                                        \
	else                                                     \
	{                                                        \
		threadIndex = 0;                                     \
		state = __LINE__;                                    \
	}                                                        \
	case __LINE__:

#define KERNEL_END \
	}              \
	}

struct CoroutineDispatcher
{
	void Dispatch()
	{
		KERNEL_BEGIN

		printf("[CoroutineDispatcher] Thread %u Initialized. \n", threadIndex);

		GROUP_BARRIER

		printf("[CoroutineDispatcher] Thread %u Task A Done. \n", threadIndex);

		GROUP_BARRIER

		printf("[CoroutineDispatcher] Thread %u Task B Done. \n", threadIndex);

		GROUP_BARRIER

		printf("[CoroutineDispatcher] Thread %u Finalized. \n", threadIndex);

		KERNEL_END
	}
};

struct resumable_thing
{
	struct promise_type
	{
		resumable_thing get_return_object()
		{
			return resumable_thing(std::coroutine_handle<promise_type>::from_promise(*this));
		}
		auto initial_suspend() { return std::suspend_never{}; }
		auto final_suspend() noexcept { return std::suspend_never{}; }
		void return_void() {}

		void unhandled_exception() {}
	};
	std::coroutine_handle<promise_type> _coroutine = nullptr;
	resumable_thing() = default;
	resumable_thing(resumable_thing const&) = delete;
	resumable_thing& operator=(resumable_thing const&) = delete;
	resumable_thing(resumable_thing&& other)
		: _coroutine(other._coroutine)
	{
		other._coroutine = nullptr;
	}
	resumable_thing& operator=(resumable_thing&& other)
	{
		if (&other != this)
		{
			_coroutine = other._coroutine;
			other._coroutine = nullptr;
		}
	}
	explicit resumable_thing(std::coroutine_handle<promise_type> coroutine)
		: _coroutine(coroutine)
	{
	}
	~resumable_thing()
	{
		if (_coroutine && _coroutine.done())
		{
			_coroutine.destroy();
		}
	}
	void resume()
	{
		if (!_coroutine.done())
		{
			_coroutine.resume();
		}
	}

	bool is_done() const { return _coroutine.done(); }
};

bool sHasRemainingResumable = true;

resumable_thing counter(int i)
{
	// std::unordered_map<uint16_t, uint16_t> completeThreadNums;
	// auto IncrementCompleteThreadNum = [&completeThreadNums](int lineNumber) {
	//	auto iter = completeThreadNums.find(lineNumber);
	//	if (iter == completeThreadNums.cend())
	//	{
	//		completeThreadNums[lineNumber] = 1;
	//		return uint16_t(1);
	//	}
	//	else
	//	{
	//		iter->second++;
	//		return iter->second;
	//	}
	// };

	printf("[ThreadDispatcher] Thread %u Initialized. \n", i);

	co_await std::suspend_always{};

	printf("[ThreadDispatcher] Thread %u Task A Done. \n", i);

	co_await std::suspend_always{};

	printf("[ThreadDispatcher] Thread %u Finalized. \n", i);

	sHasRemainingResumable = false;
	co_return;
}

int main()
{
	resumable_thing the_counter = counter(0);

	while (sHasRemainingResumable)
	{
		if (!the_counter._coroutine.done())
		{
			the_counter.resume();
		}
	}

	return 0;
}