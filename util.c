#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* escape_char(char * str){

 	//Guessing
	int chars_to_replace_position [100];
	int chars_to_replace_position_idx = 0;
	int i, j;
	int start = 0;
	int end = 0;
	int pointer = 0;

	char lookup[] = {'\"', '\\'};

	char *buffer;


	for(i = 0; i < (int)strlen(str); i++){
		if(str[i] == lookup[0] || str[i] == lookup[1]){
			chars_to_replace_position[chars_to_replace_position_idx++] = i;
		}
	}


	if(chars_to_replace_position_idx){

		buffer = malloc(sizeof(char) * (strlen(str) + chars_to_replace_position_idx + 1));
		

		for (j = 0; j < chars_to_replace_position_idx; j++){

			end = chars_to_replace_position[j];
			memcpy(buffer + pointer, str + start, (end - start));
			pointer += (end - start);
			buffer[pointer] = '\\';
			pointer++;
			buffer[pointer] = str[end];
			pointer++;
			start = end + 1;

		}

		memcpy(buffer + pointer, str + start, (strlen(str) - start));
		pointer++;
		buffer[pointer] = '\0';

	}else{
		buffer = str;
	}
	
	return buffer;
}
