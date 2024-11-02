//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// Use of this source code is subject to the terms of the Microsoft shared
// source or premium shared source license agreement under which you licensed
// this source code. If you did not accept the terms of the license agreement,
// you are not authorized to use this source code. For the terms of the license,
// please see the license agreement between you and Microsoft or, if applicable,
// see the SOURCE.RTF on your install media or the root of your tools installation.
// THE SOURCE CODE IS PROVIDED "AS IS", WITH NO WARRANTIES.
//
#include "compcat.h"
#include "..\mystring\mystring.h"

int main(int argc, char *argv[]){
  DWORD length = 0;
  bool split = false;

  int i;
  
  if(argc < 2 || strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "/?") == 0){
    goto USAGE;
  }

  for(i = 1; i < argc - 1; i++){
    if(_stricmp(argv[i], "-s") == 0){
      split = true;
    }
    else if(_stricmp(argv[i], "-l") == 0){
      i++;
      length = strtoul(argv[i], NULL, 16);

      if(!length){
        fprintf(stderr, "Error: Invalid length specified\n");
        goto USAGE;
      }
    }
    else
      goto USAGE;
  }

  if(split && !length){
    fprintf(stderr, "Error: Length must be specified if split option is desired\n");
    exit(1);
  }

  if(split || length){
    // use command line parameters
    if(compact_bin(tounicode(argv[argc - 1]), length, split)) // && compress_bin(argv[1]))
      printf("Done\n");
  }
  else{
    // use defaults
    if(compact_bin(tounicode(argv[argc - 1]))) // && compress_bin(argv[1]))
      printf("Done\n");
  }
  
  return 0;
  
USAGE:
    fprintf(stderr, "\nUsage: %s [ options ] filename\n\n", argv[0]);
    fprintf(stderr, "Options:\n"
                    "  -l length   Max length of record in hex\n"
                    "  -s          Allow splitting of records to adhere to max length\n"
                    "              (WARNING: this may split a large compressed file across\n"
                    "                        multiple records and break UpdateXIP/Diffbin)"
                    );
    exit(1);  
}
