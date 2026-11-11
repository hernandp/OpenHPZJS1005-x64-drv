// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
/* Test-only Windows entry point; protocol reference sources are unmodified. */
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
int optind=1;
char* optarg;
int getopt(int argc,char* const argv[],const char* options) {
    const char* option;
    char c;
    if(optind>=argc || argv[optind][0]!='-' || !argv[optind][1]) return -1;
    c=argv[optind][1]; option=strchr(options,c);
    if(!option) {++optind;return '?';}
    if(option[1]==':') {
        if(argv[optind][2]) optarg=argv[optind++]+2;
        else {++optind;if(optind>=argc)return '?';optarg=argv[optind++];}
    } else ++optind;
    return c;
}
int reference_main(int argc,char** argv);
int main(int argc,char** argv) {
    _setmode(_fileno(stdin),_O_BINARY);
    _setmode(_fileno(stdout),_O_BINARY);
    return reference_main(argc,argv);
}
