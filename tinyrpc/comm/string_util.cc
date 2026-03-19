#include <map>
#include <string>
#include "tinyrpc/comm/string_util.h"
#include "tinyrpc/comm/log.h"


namespace tinyrpc {

void StringUtil::SplitStrToMap(const std::string& str, const std::string& split_str, 
    const std::string& joiner, std::map<std::string, std::string>& res) {
    //报错
    if (str.empty() || split_str.empty() || joiner.empty()) {
        DebugLog << "str or split_str or joiner_str is empty";
        return;
    }
    std::string tmp = str;
    std::vector<std::string> vec;
    SplitStrToVector(tmp, split_str, vec);

    for (auto i : vec) {
    if (!i.empty()) {
        //查找joiner字符串里任意一个字符第一次出现的位置
        size_t j = i.find_first_of(joiner);
        //确保在字符串i中找到了joiner中的任意字符，并且该字符不是字符串i的第一个字符
        if (j != i.npos && j != 0) {
            //从字符串i的开头到位置j之间的子字符串作为key，从位置j加上joiner长度到字符串i末尾的子字符串作为value
            std::string key = i.substr(0, j);
            std::string value = i.substr(j + joiner.length(), i.length() - j - joiner.length());
            DebugLog << "insert key = " << key << ", value=" << value;
            res[key.c_str()] = value;
        }
    }
  }
}


void StringUtil::SplitStrToVector(const std::string& str, const std::string& split_str, 
    std::vector<std::string>& res) {

  if (str.empty() || split_str.empty()) {
    // DebugLog << "str or split_str is empty";
    return;
  }
  std::string tmp = str;
  //如果tmp字符串的末尾不是split_str字符串，则在tmp字符串的末尾添加split_str字符串
  if (tmp.substr(tmp.length() - split_str.length(), split_str.length()) != split_str) {
    tmp += split_str;
  }

  while (1) {
    //在tmp字符串中查找split_str字符串第一次出现的位置，并将该位置赋值给变量i
    size_t i = tmp.find_first_of(split_str);
    //如果在tmp字符串中没有找到split_str字符串，则返回
    if (i == tmp.npos) {
      return;
    }
    int l = tmp.length();
    std::string x = tmp.substr(0, i);
    tmp = tmp.substr(i + split_str.length(), l - i - split_str.length());
    if (!x.empty()) {
        //添加x字符串到res向量的末尾，并且使用std::move将x字符串的资源转移到res向量中，避免不必要的复制操作，提高性能
      res.push_back(std::move(x));
    }
  }

}
}
