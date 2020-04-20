#define BOOST_DATE_TIME_NO_LIB

#include <iostream>
#include <mutex>
#include <string>
#include <cstdlib>
#include <functional>
#include <utility>
#include <thread>
#include <chrono>
#include <ctime>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

using void_allocator = boost::interprocess::allocator <void, boost::interprocess::managed_shared_memory::segment_manager>;
using char_allocator = boost::interprocess::allocator <char, boost::interprocess::managed_shared_memory::segment_manager>;
using char_string = boost::interprocess::basic_string <char, std::char_traits <char>, char_allocator>;
using string_allocator = boost::interprocess::allocator <char_string, boost::interprocess::managed_shared_memory::segment_manager>;
using map_value = std::pair <const char_string, char_string>;
using map_value_allocator = boost::interprocess::allocator <map_value, boost::interprocess::managed_shared_memory::segment_manager>;
using map_type = boost::interprocess::map <char_string, char_string, std::greater<char_string>, map_value_allocator>;

void print_map(map_type* data) {
	for (auto it = data->crbegin(); it != data->crend(); ++it)
		std::cout << it->first << " " << it->second << std::endl;
}

void wait_block(boost::interprocess::interprocess_condition* condition, boost::interprocess::interprocess_mutex* mutex,
	map_type* data) {
	int num = data->size();
	{
		std::unique_lock <boost::interprocess::interprocess_mutex> lock(*mutex);
		condition->wait(lock, [&data, num] {
			if (data->size() != num) {
				char_string val = data->begin()->first;
				return val.back() != '2';
			}
			else
				return false;
			});
		std::cout << "new message: " << data->begin()->second << std::endl;
	}
	//std::cout << "whole messages" << std::endl;
	//print_map(data);
	wait_block(condition, mutex, data);
}

void write_block(boost::interprocess::interprocess_condition* condition, boost::interprocess::interprocess_mutex* mutex,
	map_type* data, char_allocator ca) {
	char_string val(ca);
	//char_string key(ca);
	std::cin >> val;
	{
		std::scoped_lock <boost::interprocess::interprocess_mutex> lock(*mutex);
		auto cur = std::chrono::steady_clock::now();
		auto delta = cur.time_since_epoch();
		std::string s = std::to_string(delta.count() / 100) + "_2";
		char_string key(s.c_str(), ca);
		data->insert(std::pair<char_string, char_string>(key, val));
	}
	condition->notify_all();
	write_block(condition, mutex, data, ca);
}

int main() {
	system("pause");
	const std::string shared_memory_name = "shared_memory_name";
	//boost::interprocess::shared_memory_object::remove(shared_memory_name.c_str());
	boost::interprocess::managed_shared_memory shared_memory(boost::interprocess::open_or_create, shared_memory_name.c_str(), 65536);

	const std::string mutex_name = "mutex";
	const std::string condition_name = "condiiton";
	auto mutex = shared_memory.find_or_construct <boost::interprocess::interprocess_mutex>(mutex_name.c_str())();
	auto condition = shared_memory.find_or_construct <boost::interprocess::interprocess_condition>(condition_name.c_str())();

	map_value_allocator alloc_init(shared_memory.get_segment_manager());
	map_type* data = shared_memory.find_or_construct<map_type>("map") (std::greater<char_string>(), alloc_init);

	if (!data->empty()) {
		print_map(data);
	}

	char_allocator ca(shared_memory.get_segment_manager());
	std::thread t(wait_block, std::ref(condition), std::ref(mutex), std::ref(data));
	std::thread t1(write_block, std::ref(condition), std::ref(mutex), std::ref(data), ca);
	t.join();
	t1.join();

	boost::interprocess::shared_memory_object::remove(shared_memory_name.c_str());
	return 0;
}