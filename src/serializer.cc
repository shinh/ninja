// Copyright 2016 Google Inc. All Rights Reserved.
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

#include "serializer.h"

#include <assert.h>
#include <stdlib.h>

Serializer::Serializer(FILE* fp)
    : fp_(fp) {
}

void Serializer::SerializeInt(int v) {
  size_t l = buf_.size();
  buf_.resize(l + 4);
  memcpy(&buf_[l], &v, sizeof(v));
}

void Serializer::SerializeString(StringPiece s) {
  SerializeInt(s.len_);
  size_t l = buf_.size();
  buf_.resize(l + s.len_);
  memcpy(&buf_[l], s.str_, s.len_);
  buf_ += '\0';
}

void Serializer::Close() {
  fwrite(buf_.data(), buf_.size(), 1, fp_);
  fclose(fp_);
}

Deserializer::Deserializer(FILE* fp)
    : fp_(fp) {
  fseek(fp_, 0, SEEK_END);
  size_t len = ftell(fp_);
  buf_ = static_cast<char*>(malloc(len));
  fseek(fp_, 0, SEEK_SET);
  fread(buf_, len, 1, fp_);
  ptr_ = buf_;
}

int Deserializer::DeserializeInt() {
  int v = *reinterpret_cast<int*>(ptr_);
  ptr_ += 4;
  return v;
}

bool Deserializer::DeserializeString(string* s) {
  int len = DeserializeInt();
  if (len < 0)
    return false;
  s->resize(len);
  len++;
  memcpy(&(*s)[0], ptr_, len);
  ptr_ += len;
  return true;
}

bool Deserializer::DeserializeStringPiece(StringPiece* s) {
  int len = DeserializeInt();
  if (len < 0)
    return false;
  s->str_ = ptr_;
  s->len_ = len;
  ptr_ += len + 1;
  return true;
}

void Deserializer::Close() {
  fclose(fp_);
}
