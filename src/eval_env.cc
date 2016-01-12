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

#include "eval_env.h"

#include <assert.h>
#include <stdio.h>

#include "serializer.h"

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
    //printf("%p %zu %s\n", i->first.str_, i->first.len_, i->first.AsString().c_str());
    if (i->second == RAW)
#ifdef USE_STRING_PIECE
      result.append(i->first.str_, i->first.len_);
#else
      result += i->first;
#endif
    else {
#ifdef USE_STRING_PIECE
      // XXX: Slow!
      result.append(env->LookupVariable(i->first.AsString()));
#else
      result.append(env->LookupVariable(i->first.AsString()));
#endif
    }
  }
  return result;
}

void EvalString::AddText(StringPiece text) {
#ifdef USE_STRING_PIECE
  // Add it to the end of an existing RAW token if possible.
  if (!parsed_.empty() && parsed_.back().second == RAW) {
    buf_.back().append(text.str_, text.len_);
    parsed_.back().first = buf_.back();
  } else {
    buf_.push_back(text.AsString());
    parsed_.push_back(make_pair(StringPiece(buf_.back()), RAW));
  }
#else
  // Add it to the end of an existing RAW token if possible.
  if (!parsed_.empty() && parsed_.back().second == RAW) {
    parsed_.back().first.append(text.str_, text.len_);
  } else {
    parsed_.push_back(make_pair(text.AsString(), RAW));
  }
#endif
}

void EvalString::AddSpecial(StringPiece text) {
#ifdef USE_STRING_PIECE
  buf_.push_back(text.AsString());
  parsed_.push_back(make_pair(StringPiece(buf_.back()), SPECIAL));
#else
  parsed_.push_back(make_pair(text.AsString(), SPECIAL));
#endif
}

string EvalString::Serialize() const {
  string result;
  for (TokenList::const_iterator i = parsed_.begin();
       i != parsed_.end(); ++i) {
    result.append("[");
    if (i->second == SPECIAL)
      result.append("$");
#ifdef USE_STRING_PIECE
    result.append(i->first.str_, i->first.len_);
#else
    result += i->first.str_;
#endif
    result.append("]");
  }
  return result;
}

void EvalString::Serialize2(Serializer* serializer) const {
  serializer->SerializeInt(parsed_.size());
  for (TokenList::const_iterator i = parsed_.begin();
       i != parsed_.end(); ++i) {
    serializer->SerializeString(i->first);
    serializer->SerializeInt(i->second);
  }
}

bool EvalString::Deserialize(Deserializer* deserializer) {
  int size = deserializer->DeserializeInt();
  if (size < 0)
    return false;
  StringPiece s;
  for (int i = 0; i < size; i++) {
    if (!deserializer->DeserializeStringPiece(&s))
      return false;
    int type = deserializer->DeserializeInt();
    if (type != RAW && type != SPECIAL)
      return false;
    //fprintf(stderr, "%d %s(%d)\n", i, s.c_str(), type);
    parsed_.push_back(make_pair(s, static_cast<TokenType>(type)));
  }
  return true;
}

void BindingEnv::Serialize(Serializer* serializer) const {
  serializer->SerializeInt(bindings_.size());
  for (map<string, string>::const_iterator it = bindings_.begin();
       it != bindings_.end(); ++it) {
    serializer->SerializeString(it->first);
    serializer->SerializeString(it->second);
  }
}

bool BindingEnv::Deserialize(Deserializer* deserializer) {
  int bindings_size = deserializer->DeserializeInt();
  if (bindings_size < 0)
    return false;
  string k, v;
  for (int i = 0; i < bindings_size; ++i) {
    if (!deserializer->DeserializeString(&k))
      return false;
    if (!deserializer->DeserializeString(&v))
      return false;
    //fprintf(stderr, "binding %s=%s %p\n", k.c_str(), v.c_str(), this);
    if (!bindings_.insert(make_pair(k, v)).second)
      return false;
  }
  return true;
}
