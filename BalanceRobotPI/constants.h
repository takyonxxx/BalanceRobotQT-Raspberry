#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QString>
#include <iostream>

using namespace std;

enum SType
{
    TR,
    EN
};

static QString baseApi = "https://speech.googleapis.com/v1/speech:recognize";
static QString baseWikiApi = "https://tr.wikipedia.org/w/api.php";
static QString baseWikiSummaryApi = "https://tr.wikipedia.org/api/rest_v1/page/summary/";
static QString apiKey = "AIzaSyCY8Xg5wfn6Ld67287SGDBQPZvGCEN6Fsg";

static char * appendChar(char * string1, char * string2)
{
    char * result = NULL;
    asprintf(&result, "%s%s", string1, string2);
    return result;
}

static void execCommand(char* cmd)
{
    auto command = appendChar(cmd, (char*)">>/dev/null 2>>/dev/null");

    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(command, "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != nullptr)
                result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
}


#endif // CONSTANTS_H
