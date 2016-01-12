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

#ifndef NINJA_SERIALIZER_H_
#define NINJA_SERIALIZER_H_

#include <stdio.h>

#include "string_piece.h"

class Serializer {
 public:
  explicit Serializer(FILE* fp);
  void SerializeInt(int v);
  void SerializeString(StringPiece s);
  void Close();

 private:
  FILE* fp_;
  string buf_;
};

class Deserializer {
 public:
  explicit Deserializer(FILE* fp);
  int DeserializeInt();
  bool DeserializeString(string* s);
  bool DeserializeStringPiece(StringPiece* s);
  void Close();

 private:
  FILE* fp_;
  char* buf_;
  char* ptr_;
};

#endif  // NINJA_SERIALIZER_H_
