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
#include <atomic>
#include <iterator>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

using char_allocator = boost::interprocess::allocator <char, boost::interprocess::managed_shared_memory::segment_manager>;
using char_string = boost::interprocess::basic_string <char, std::char_traits <char>, char_allocator>;
using string_allocator = boost::interprocess::allocator <char_string, boost::interprocess::managed_shared_memory::segment_manager>;
using map_value = std::pair <const int, char_string>;
using map_value_allocator = boost::interprocess::allocator <map_value, boost::interprocess::managed_shared_memory::segment_manager>;
using map_type = boost::interprocess::map <int, char_string, std::less<int>, map_value_allocator>;

std::atomic_int last_in_local = -1;
std::atomic_bool b = true;


void print_map(map_type* data) {
	for (auto it = data->crbegin(); it != data->crend(); ++it)
		std::cout << it->first << " " << it->second << std::endl;
}

void wait_block(boost::interprocess::interprocess_condition* condition, boost::interprocess::interprocess_mutex* mutex,
	map_type* data, int* last_in, int* num_of_users) {
	while (!b)
	{
		std::unique_lock <boost::interprocess::interprocess_mutex> lock(*mutex);
		condition->wait(lock, [&data, &last_in] {
			return (last_in_local != *last_in);
		});
		if (b) {
			for (int i = 0; i < *last_in - last_in_local; ++i)
				std::cout << "new message: " << std::next(data->crbegin(), i)->second << std::endl;
			last_in_local = *last_in;
		}
		else
			--(*num_of_users);
	}
	std::cout << "note: finish from wait_block" << std::endl;
}

void write_block(boost::interprocess::interprocess_condition* condition, boost::interprocess::interprocess_mutex* mutex,
	map_type* data, int* last_in, boost::interprocess::managed_shared_memory::segment_manager* sg) {
	char_allocator ca(sg);
	char_string val(ca);
	val = "empty";
	while (val != "EXIT") {
		std::cin >> val;
		{
			std::scoped_lock <boost::interprocess::interprocess_mutex> lock(*mutex);
			++(*last_in);
			last_in_local = *last_in;
			data->insert(std::make_pair(*last_in, val));
		}
		if (val == "EXIT") {
			b = false;
			++last_in_local;
		}
		condition->notify_all();
	}
	std::cout << "note: finish from write_block" << std::endl;
}

int main() {
	std::cout << "note: tap to start" << std::endl;
	system("pause");
	const std::string shared_memory_name = "shared_memory_name";
	boost::interprocess::managed_shared_memory shared_memory(boost::interprocess::open_or_create, shared_memory_name.c_str(), 65536);

	const std::string mutex_name = "mutex";
	const std::string condition_name = "condiiton";
	auto mutex = shared_memory.find_or_construct <boost::interprocess::interprocess_mutex>(mutex_name.c_str())();
	auto condition = shared_memory.find_or_construct <boost::interprocess::interprocess_condition>(condition_name.c_str())();
	auto last_in = shared_memory.find_or_construct <int>("last_in") (-1);
	auto num_of_users = shared_memory.find_or_construct <int>("num_of_users") (-1);

	map_value_allocator alloc_init(shared_memory.get_segment_manager());
	map_type* data = shared_memory.find_or_construct<map_type>("map") (std::less<int>(), alloc_init);

	char_allocator ca(shared_memory.get_segment_manager());
	++(*num_of_users);
	std::thread t(wait_block, std::ref(condition), std::ref(mutex), std::ref(data), std::ref(last_in), std::ref(num_of_users));
	std::thread t1(write_block, std::ref(condition), std::ref(mutex), std::ref(data), std::ref(last_in), shared_memory.get_segment_manager());
	t.join();
	t1.join();

	if (*num_of_users == -1) {
		boost::interprocess::shared_memory_object::remove(shared_memory_name.c_str());
		std::cout << "note: shm deleted" << std::endl;
	}
	std::cout << "note: end of main thread" << std::endl;
	return 0;
}