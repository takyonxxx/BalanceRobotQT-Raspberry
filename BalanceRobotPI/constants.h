#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QString>
#include <QDebug>
#include <iostream>
#include <deque>
#include <vector>

using namespace std;

template <typename T>
double mov_avg(vector<T> vec, int len){
  deque<T> dq = {};
  for(auto i = 0;i < vec.size();i++){
    if(i < len){
      dq.push_back(vec[i]);
    }
    else {
      dq.pop_front();
      dq.push_back(vec[i]);
    }
  }
  double cs = 0;
  for(auto i : dq){
    cs += i;
  }
  return cs / len;
}

enum SType
{
    TR,
    EN
};

static QString baseSpeechApi = "https://speech.googleapis.com/v1/speech:recognize";
static QString apiSpeechKey = "AIzaSyCY8Xg5wfn6Ld67287SGDBQPZvGCEN6Fsg";
static QString baseDuckduckgo = "http://api.duckduckgo.com";

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
