#include <thread>
#include <iostream>
#include <fstream>
#include <unistd.h>

#define GB (1024 * 1024 * 1024)

int main() {
	long long* p = (long long *) malloc(sizeof(long long) * GB);
	while (1) {
		for (long long i = 0; i < GB; i++) {
			long long r = rand() % GB;
			*(p + r) += 10;
		}
	}
	free(p);
	return 0;
}
