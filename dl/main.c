#include <stdio.h>
#include <stdlib.h>

#include "ringbuffer.h"


int main(){
	int i, c;
	unsigned char* in;
	int count = 0;

	while(1){
		printf("0. Insert, 1. Get, 2. Exit \n");
		scanf("%d", &c);

		if (c == 0){
			in = (char*)malloc(2);
			in[0] = count++;
			insert(in);
		}
		else if (c == 1){
			in = get();

			if (in != NULL)
				printf("Data = %d\n", in[0]);
		}
		else if (c == 2){
			break;
		}
	}

	return 0;
}


