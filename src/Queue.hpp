#pragma once

#include <queue>
#include <mutex>
#include <optional>

template <typename Type>
class ThreadSafeQueue
{
private:
	std::queue<Type> queue;
	std::mutex mutex;

public:
	void Push(Type item)
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.push(std::move(item));
	}

	std::optional<Type> TryPop()
	{
		std::lock_guard<std::mutex> lock(mutex);

		if (queue.empty()) { return std::nullopt; }

		Type item = std::move(queue.front());
		queue.pop();
		return item;
	}

	bool Empty()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return queue.empty();
	}
};