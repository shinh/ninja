// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <stdio.h>

#include "eval_env.h"

string BindingEnv::LookupVariable(const string& var) {
  map<string, string>::iterator i = bindings_.find(var);
  if (i != bindings_.end())
    return i->second;
  if (parent_)
    return parent_->LookupVariable(var);
  return "";
}

void BindingEnv::AddBinding(const string& key, const string& val) {
  bindings_[key] = val;
}

void BindingEnv::AddRule(const Rule* rule) {
  assert(LookupRuleCurrentScope(rule->name()) == NULL);
  rules_[rule->name()] = rule;
}

const Rule* BindingEnv::LookupRuleCurrentScope(const string& rule_name) {
  map<string, const Rule*>::iterator i = rules_.find(rule_name);
  if (i == rules_.end())
    return NULL;
  return i->second;
}

const Rule* BindingEnv::LookupRule(const string& rule_name) {
  map<string, const Rule*>::iterator i = rules_.find(rule_name);
  if (i != rules_.end())
    return i->second;
  if (parent_)
    return parent_->LookupRule(rule_name);
  return NULL;
}

void Rule::AddBinding(const string& key, const EvalString& val) {
  bindings_[key] = val;
}

const EvalString* Rule::GetBinding(const string& key) const {
  Bindings::const_iterator i = bindings_.find(key);
  if (i == bindings_.end())
    return NULL;
  return &i->second;
}

// static
bool Rule::IsReservedBinding(const string& var) {
  return var == "command" ||
      var == "depfile" ||
      var == "description" ||
      var == "deps" ||
      var == "generator" ||
      var == "pool" ||
      var == "restat" ||
      var == "rspfile" ||
      var == "rspfile_content" ||
      var == "msvc_deps_prefix";
}

const map<string, const Rule*>& BindingEnv::GetRules() const {
  return rules_;
}

string BindingEnv::LookupWithFallback(const string& var,
                                      const EvalString* eval,
                                      Env* env) {
  map<string, string>::iterator i = bindings_.find(var);
  if (i != bindings_.end())
    return i->second;

  if (eval)
    return eval->Evaluate(env);

  if (parent_)
    return parent_->LookupVariable(var);

  return "";
}

string EvalString::Evaluate(Env* env) const {
  string result;
  for (TokenList::const_iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == RAW)
      result.append(i->first);
    else {
#if 0
      if (i->first == "root") {
        fprintf(stderr, "lookup %s=%s env=%p\n", i->first.c_str(), env->LookupVariable(i->first).c_str(), env);
      }
#endif
      result.append(env->LookupVariable(i->first));
    }
  }
  return result;
}

void EvalString::AddText(StringPiece text) {
  // Add it to the end of an existing RAW token if possible.
  if (!parsed_.empty() && parsed_.back().second == RAW) {
    parsed_.back().first.append(text.str_, text.len_);
  } else {
    parsed_.push_back(make_pair(text.AsString(), RAW));
  }
}
void EvalString::AddSpecial(StringPiece text) {
  parsed_.push_back(make_pair(text.AsString(), SPECIAL));
}

string EvalString::Serialize() const {
  string result;
  for (TokenList::const_iterator i = parsed_.begin();
       i != parsed_.end(); ++i) {
    result.append("[");
    if (i->second == SPECIAL)
      result.append("$");
    result.append(i->first);
    result.append("]");
  }
  return result;
}

namespace {

void SerializeInt(FILE* fp, int v) {
  size_t r = fwrite(&v, sizeof(v), 1, fp);
  assert(r == 1);
}

void SerializeString(FILE* fp, StringPiece s) {
  SerializeInt(fp, s.len_);
  size_t r = fwrite(s.str_, 1, s.len_, fp);
  assert(r == s.len_);
}

int DeserializeInt(FILE* fp) {
  int v;
  size_t r = fread(&v, sizeof(v), 1, fp);
  if (r != 1)
    return -1;
  return v;
}

bool DeserializeString(FILE* fp, string* s) {
  int len = DeserializeInt(fp);
  if (len < 0)
    return false;
  s->resize(len);
  size_t r = fread(&(*s)[0], 1, s->size(), fp);
  if (r != s->size())
    return false;
  return true;
}

}

void EvalString::Serialize2(FILE* fp) const {
  SerializeInt(fp, parsed_.size());
  for (TokenList::const_iterator i = parsed_.begin();
       i != parsed_.end(); ++i) {
    SerializeString(fp, i->first);
    SerializeInt(fp, i->second);
  }
}

bool EvalString::Deserialize(FILE* fp) {
  int size = DeserializeInt(fp);
  if (size < 0)
    return false;
  string s;
  for (int i = 0; i < size; i++) {
    if (!DeserializeString(fp, &s))
      return false;
    int type = DeserializeInt(fp);
    if (type != RAW && type != SPECIAL)
      return false;
    //fprintf(stderr, "%d %s(%d)\n", i, s.c_str(), type);
    parsed_.push_back(make_pair(s, static_cast<TokenType>(type)));
  }
  return true;
}

void BindingEnv::Serialize(FILE* fp) const {
  SerializeInt(fp, bindings_.size());
  for (map<string, string>::const_iterator it = bindings_.begin();
       it != bindings_.end(); ++it) {
    SerializeString(fp, it->first);
    SerializeString(fp, it->second);
  }
}

bool BindingEnv::Deserialize(FILE* fp) {
  int bindings_size = DeserializeInt(fp);
  if (bindings_size < 0)
    return false;
  string k, v;
  for (int i = 0; i < bindings_size; ++i) {
    if (!DeserializeString(fp, &k))
      return false;
    if (!DeserializeString(fp, &v))
      return false;
    //fprintf(stderr, "binding %s=%s %p\n", k.c_str(), v.c_str(), this);
    if (!bindings_.insert(make_pair(k, v)).second)
      return false;
  }
  return true;
}
