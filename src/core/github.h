#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/director.h"
#include "core/story.h"

namespace kare {

struct RepoInfo {
    std::string owner;
    std::string name;
    std::string description;
    std::string homepage;
    std::string license;
    std::string branch;
    int stars = 0;
    int forks = 0;
    int issues = 0;
    std::vector<std::string> topics;
    std::vector<std::pair<std::string, double>> languages;
    int files = 0;
    std::vector<std::string> tooling;
    bool has_tests = false;
    bool has_docs = false;
    std::string readme;
};

bool find_github_repo(const std::string& text, std::string& owner, std::string& repo, size_t* begin = nullptr,
                      size_t* end = nullptr);
std::string strip_github_url(const std::string& text);
bool fetch_repo(const std::string& owner, const std::string& repo, const std::string& token, RepoInfo& out,
                std::string& err);

std::string clean_markdown(const std::string& line);
std::string readme_summary(const std::string& markdown);
std::vector<std::string> readme_features(const std::string& markdown, size_t limit);
std::string compact_count(int n);
std::string repo_note(const RepoInfo& info, bool turkish);
std::string repo_brief(const RepoInfo& info);
Storyboard plan_repo_local(const RepoInfo& info, const PlanInput& in);

}  // namespace kare
