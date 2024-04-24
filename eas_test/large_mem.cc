#include <thread>
#include <iostream>
#include <fstream>
#include <unistd.h>

int main() {
	while (1) {
		int* p = (int*) malloc(sizeof(int) * 1024 * 1024 * 8);
		for (int i = 0; i < 1024 * 1024; i++) {
			int r = rand() % (1024 * 1024 * 8);
			*(p+r) = 10;
		}
		free(p);
	}
	return 0;
}
