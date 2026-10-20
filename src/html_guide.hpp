// Local HTML guides are read as content only; scripts and styles are never executed.
#pragma once
#include <regex>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace htmlguide {
inline std::string removeBlocks(std::string s, const std::string& tag) {
    std::string lower = s;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    size_t start;
    while ((start = lower.find("<" + tag)) != std::string::npos) {
        size_t close = lower.find("</" + tag + ">", start);
        if (close == std::string::npos) break;
        size_t length = close + tag.size() + 3 - start;
        s.erase(start, length); lower.erase(start, length);
    }
    return s;
}
inline std::string replace(const std::string& s, const char* pattern, const std::string& value) {
    return std::regex_replace(s, std::regex(pattern, std::regex::icase), value);
}
inline std::string entities(std::string s) {
    const std::pair<const char*, const char*> named[] = {
        {"&nbsp;", " "}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"},
        {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}
    };
    for (auto& entry : named) {
        size_t p = 0;
        while ((p = s.find(entry.first, p)) != std::string::npos) {
            s.replace(p, strlen(entry.first), entry.second); p += strlen(entry.second);
        }
    }
    std::regex numeric("&#(x[0-9a-f]+|[0-9]+);", std::regex::icase);
    std::smatch match;
    std::string out;
    while (std::regex_search(s, match, numeric)) {
        out += match.prefix().str();
        std::string code = match[1];
        bool hex = code[0] == 'x' || code[0] == 'X';
        unsigned long cp = strtoul(code.c_str() + (hex ? 1 : 0), nullptr, hex ? 16 : 10);
        if (cp > 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xc0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) {
                out += (char)(0xe0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63));
            } else {
                out += (char)(0xf0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63));
                out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63));
            }
        }
        s = match.suffix();
    }
    return out + s;
}
inline std::string text(std::string s) {
    s = replace(s, "<[^>]*>", " ");
    s = entities(s);
    s = replace(s, "[ \\t\\r\\n]+", " ");
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
inline std::string attribute(const std::string& tag, const char* name) {
    std::regex expression(std::string("\\b") + name + "\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(tag, m, expression)) return "";
    return entities(m[1].matched ? m[1].str() : m[2].matched ? m[2].str() : m[3].str());
}
inline std::string markdown(std::string s) {
    s = replace(s, "<!--[\\s\\S]*?-->", "");
    for (auto tag : {"head", "script", "style"}) s = removeBlocks(s, tag);
    // The bundled source has a missing '>' before the final anchor's Chinese note.
    s = replace(s, "</a（", "</a>（");
    std::regex link("<a\\b[^>]*>([\\s\\S]*?)</a\\s*>", std::regex::icase);
    std::smatch linkMatch;
    std::string links;
    while (std::regex_search(s, linkMatch, link)) {
        links += linkMatch.prefix().str() + linkMatch[1].str();
        auto url = attribute(linkMatch.str().substr(0, linkMatch.str().find('>') + 1), "href");
        if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) links += "（" + url + "）";
        s = linkMatch.suffix();
    }
    s = links + s;
    // Images are tokenized before removing tags; no remote resource is downloaded.
    std::regex image("<img\\b[^>]*>", std::regex::icase);
    std::smatch m;
    std::string out;
    while (std::regex_search(s, m, image)) {
        out += m.prefix().str();
        auto src = attribute(m.str(), "src");
        if (!src.empty()) out += "\n\n![](" + src + ")\n\n";
        s = m.suffix();
    }
    s = out + s;
    s = replace(s, "<h1\\b[^>]*>", "\n\n# ");
    s = replace(s, "<h2\\b[^>]*>", "\n\n## ");
    s = replace(s, "<h[3-6]\\b[^>]*>", "\n\n### ");
    s = replace(s, "<li\\b[^>]*>", "\n- ");
    s = replace(s, "<br\\s*/?>|</?(?:div|p|table|tr|h[1-6]|ul|ol)\\b[^>]*>", "\n\n");
    s = replace(s, "</(?:td|th)\\s*>", " · ");
    s = replace(s, "<[^>]*>", "");
    s = entities(s);
    s = replace(s, "[ \\t]+", " ");
    s = replace(s, "\\n[ \\t]+", "\n");
    s = replace(s, "\\n{3,}", "\n\n");
    return s;
}
struct Entry { std::string title, subtitle, image, body; };
struct Document { std::string title, overview; std::vector<Entry> entries; };
inline Document parse(const std::string& html) {
    Document doc;
    std::smatch m;
    if (std::regex_search(html, m, std::regex("<title\\b[^>]*>([\\s\\S]*?)</title>", std::regex::icase))) doc.title = text(m[1]);
    std::regex row("<tr\\b[^>]*>([\\s\\S]*?)</tr>", std::regex::icase);
    std::regex cell("<td\\b[^>]*>([\\s\\S]*?)</td>", std::regex::icase);
    std::regex img("<img\\b[^>]*>", std::regex::icase);
    for (std::sregex_iterator i(html.begin(), html.end(), row), end; i != end; ++i) {
        std::string content = (*i)[1];
        std::vector<std::string> columns;
        for (std::sregex_iterator c(content.begin(), content.end(), cell); c != end; ++c) columns.push_back((*c)[1]);
        if (columns.size() != 5) continue;
        Entry entry;
        entry.title = text(replace(columns[1], "<span\\b[^>]*>[\\s\\S]*?</span>", ""));
        std::string state;
        if (std::regex_search(columns[1], m, std::regex("<span\\b[^>]*>([\\s\\S]*?)</span>", std::regex::icase))) state = text(m[1]);
        std::string area = text(columns[4]);
        entry.subtitle = state + (area.empty() || area == "-" ? "" : " · " + area);
        if (std::regex_search(columns[0], m, img)) entry.image = attribute(m.str(), "src");
        entry.body = "# " + entry.title + "\n\n" + text(columns[2]) + "\n\n" + entry.subtitle + "\n\n";
        if (!entry.image.empty()) entry.body += "![外观](" + entry.image + ")\n\n";
        entry.body += "## 获取方式与详细位置\n\n" + markdown(columns[3]);
        doc.entries.push_back(entry);
    }
    // Keep introductory notes, original group headings/counts and trailing notes as a separate entry.
    doc.overview = markdown(removeBlocks(html, "table"));
    if (doc.entries.empty()) doc.overview = markdown(html);
    return doc;
}
} // namespace htmlguide
