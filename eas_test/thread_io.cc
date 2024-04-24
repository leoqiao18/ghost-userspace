#include <thread>
#include <iostream>
#include <fstream>
#include <unistd.h>

void io_loop() {
	while (1) {
		std::ofstream file("/tmp/eas_tmp", std::ios::out);
		file << "hi";
		file.flush();
		file.close();
	}
}

int main() {
	sleep(1);
	std::thread t1(io_loop);
    
	while (1) ;

	t1.join();
}
