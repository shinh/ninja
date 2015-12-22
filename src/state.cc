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

#include "state.h"

#include <assert.h>
#include <stdio.h>

#include "edit_distance.h"
#include "graph.h"
#include "metrics.h"
#include "util.h"


void Pool::EdgeScheduled(const Edge& edge) {
  if (depth_ != 0)
    current_use_ += edge.weight();
}

void Pool::EdgeFinished(const Edge& edge) {
  if (depth_ != 0)
    current_use_ -= edge.weight();
}

void Pool::DelayEdge(Edge* edge) {
  assert(depth_ != 0);
  delayed_.insert(edge);
}

void Pool::RetrieveReadyEdges(set<Edge*>* ready_queue) {
  DelayedEdges::iterator it = delayed_.begin();
  while (it != delayed_.end()) {
    Edge* edge = *it;
    if (current_use_ + edge->weight() > depth_)
      break;
    ready_queue->insert(edge);
    EdgeScheduled(*edge);
    ++it;
  }
  delayed_.erase(delayed_.begin(), it);
}

void Pool::Dump() const {
  printf("%s (%d/%d) ->\n", name_.c_str(), current_use_, depth_);
  for (DelayedEdges::const_iterator it = delayed_.begin();
       it != delayed_.end(); ++it)
  {
    printf("\t");
    (*it)->Dump();
  }
}

// static
bool Pool::WeightedEdgeCmp(const Edge* a, const Edge* b) {
  if (!a) return b;
  if (!b) return false;
  int weight_diff = a->weight() - b->weight();
  return ((weight_diff < 0) || (weight_diff == 0 && a < b));
}

Pool State::kDefaultPool("", 0);
Pool State::kConsolePool("console", 1);
const Rule State::kPhonyRule("phony");

State::State() {
  bindings_.AddRule(&kPhonyRule);
  AddPool(&kDefaultPool);
  AddPool(&kConsolePool);
}

void State::AddPool(Pool* pool) {
  assert(LookupPool(pool->name()) == NULL);
  pools_[pool->name()] = pool;
}

Pool* State::LookupPool(const string& pool_name) {
  map<string, Pool*>::iterator i = pools_.find(pool_name);
  if (i == pools_.end())
    return NULL;
  return i->second;
}

Edge* State::AddEdge(const Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edge->pool_ = &State::kDefaultPool;
  edge->env_ = &bindings_;
  edges_.push_back(edge);
  return edge;
}

Node* State::GetNode(StringPiece path, unsigned int slash_bits) {
  Node* node = LookupNode(path);
  if (node)
    return node;
  node = new Node(path.AsString(), slash_bits);
  paths_[node->path()] = node;
  return node;
}

Node* State::LookupNode(StringPiece path) const {
  METRIC_RECORD("lookup node");
  Paths::const_iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  return NULL;
}

Node* State::SpellcheckNode(const string& path) {
  const bool kAllowReplacements = true;
  const int kMaxValidEditDistance = 3;

  int min_distance = kMaxValidEditDistance + 1;
  Node* result = NULL;
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    int distance = EditDistance(
        i->first, path, kAllowReplacements, kMaxValidEditDistance);
    if (distance < min_distance && i->second) {
      min_distance = distance;
      result = i->second;
    }
  }
  return result;
}

void State::AddIn(Edge* edge, StringPiece path, unsigned int slash_bits) {
  Node* node = GetNode(path, slash_bits);
  edge->inputs_.push_back(node);
  node->AddOutEdge(edge);
}

bool State::AddOut(Edge* edge, StringPiece path, unsigned int slash_bits) {
  Node* node = GetNode(path, slash_bits);
  if (node->in_edge())
    return false;
  edge->outputs_.push_back(node);
  node->set_in_edge(edge);
  return true;
}

bool State::AddDefault(StringPiece path, string* err) {
  Node* node = LookupNode(path);
  if (!node) {
    *err = "unknown target '" + path.AsString() + "'";
    return false;
  }
  defaults_.push_back(node);
  return true;
}

vector<Node*> State::RootNodes(string* err) {
  vector<Node*> root_nodes;
  // Search for nodes with no output.
  for (vector<Edge*>::iterator e = edges_.begin(); e != edges_.end(); ++e) {
    for (vector<Node*>::iterator out = (*e)->outputs_.begin();
         out != (*e)->outputs_.end(); ++out) {
      if ((*out)->out_edges().empty())
        root_nodes.push_back(*out);
    }
  }

  if (!edges_.empty() && root_nodes.empty())
    *err = "could not determine root nodes of build graph";

  return root_nodes;
}

vector<Node*> State::DefaultNodes(string* err) {
  return defaults_.empty() ? RootNodes(err) : defaults_;
}

void State::Reset() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i)
    i->second->ResetState();
  for (vector<Edge*>::iterator e = edges_.begin(); e != edges_.end(); ++e)
    (*e)->outputs_ready_ = false;
}

void State::Dump() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    Node* node = i->second;
    printf("%s %s [id:%d]\n",
           node->path().c_str(),
           node->status_known() ? (node->dirty() ? "dirty" : "clean")
                                : "unknown",
           node->id());
  }
  if (!pools_.empty()) {
    printf("resource_pools:\n");
    for (map<string, Pool*>::const_iterator it = pools_.begin();
         it != pools_.end(); ++it)
    {
      if (!it->second->name().empty()) {
        it->second->Dump();
      }
    }
  }
}

namespace {

void SerializeInt(FILE* fp, int v) {
  size_t r = fwrite(&v, sizeof(v), 1, fp);
  if (r != 1)
    abort();
}

void SerializeString(FILE* fp, StringPiece s) {
  SerializeInt(fp, s.len_);
  size_t r = fwrite(s.str_, 1, s.len_, fp);
  if (r != s.len_)
    abort();
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

void State::Serialize(FILE* fp) const {
  METRIC_RECORD("serialize");

  map<const Pool*, int> pool_ids;
  SerializeInt(fp, pools_.size());
  for (map<string, Pool*>::const_iterator it = pools_.begin();
       it != pools_.end(); ++it) {
    const Pool* pool = it->second;
    SerializeString(fp, pool->name());
    SerializeInt(fp, pool->depth());
    pool_ids.insert(make_pair(pool, pool_ids.size()));
  }

  vector<const BindingEnv*> bindings;
  map<const BindingEnv*, int> binding_ids;
  bindings.push_back(&bindings_);
  binding_ids.insert(make_pair(&bindings_, 0));
  for (size_t i = 0; i < edges_.size(); ++i) {
    int id = bindings.size();
    if (binding_ids.insert(make_pair(edges_[i]->env_, id)).second) {
      bindings.push_back(edges_[i]->env_);
    }
  }

  SerializeInt(fp, bindings.size());
  for (size_t i = 0; i < bindings.size(); i++) {
    bindings[i]->Serialize(fp);
  }
  for (size_t i = 0; i < bindings.size(); i++) {
    const BindingEnv* parent = bindings[i]->parent();
    if (parent) {
      //fprintf(stderr, "id=%d parent_id=%d %p %p\n", i, binding_ids[parent], bindings[i], parent);
      SerializeInt(fp, binding_ids[parent] + 1);
    } else {
      SerializeInt(fp, 0);
    }
  }

  SerializeInt(fp, paths_.size());
  int node_id = 0;
  for (Paths::const_iterator it = paths_.begin(); it != paths_.end(); ++it) {
    Node* node = it->second;
    SerializeString(fp, node->path());
    SerializeInt(fp, node->slash_bits());
    //SerializeInt(fp, node->id());
    node->set_id(node_id++);
  }

  vector<const Rule*> rules;
  map<const Rule*, int> rule_ids;
  SerializeInt(fp, edges_.size());
  for (size_t i = 0; i < edges_.size(); ++i) {
    const Edge* edge = edges_[i];

    if (edge->is_phony()) {
      SerializeInt(fp, 0);
    } else {
      const Rule* rule = edge->rule_;
      int rule_id = rule_ids.size();
      pair<map<const Rule*, int>::const_iterator, bool> p =
          rule_ids.insert(make_pair(rule, rule_id));
      if (p.second) {
        // New rule.
        rules.push_back(rule);
        SerializeInt(fp, 1);

        SerializeString(fp, rule->name());

        const Rule::Bindings& bindings = rule->bindings();
        SerializeInt(fp, bindings.size());
        for (Rule::Bindings::const_iterator it = bindings.begin();
             it != bindings.end(); ++it) {
          SerializeString(fp, it->first);
          it->second.Serialize2(fp);
        }
      } else {
        SerializeInt(fp, p.first->second + 2);
      }
    }

    map<const Pool*, int>::const_iterator found = pool_ids.find(edge->pool_);
    if (found == pool_ids.end())
      abort();
    SerializeInt(fp, found->second);

    SerializeInt(fp, edge->inputs_.size());
    for (size_t i = 0; i < edge->inputs_.size(); ++i) {
      SerializeInt(fp, edge->inputs_[i]->id());
    }

    SerializeInt(fp, edge->outputs_.size());
    for (size_t i = 0; i < edge->outputs_.size(); ++i) {
      SerializeInt(fp, edge->outputs_[i]->id());
    }

    SerializeInt(fp, edge->implicit_deps_);
    SerializeInt(fp, edge->order_only_deps_);

    if (!edge->env_)
      abort();
#if 0
    edge->env_->Serialize(fp);
#endif
    SerializeInt(fp, binding_ids[edge->env_]);
  }

  SerializeInt(fp, defaults_.size());
  for (size_t i = 0; i < defaults_.size(); ++i) {
    SerializeInt(fp, defaults_[i]->id());
  }

  for (Paths::const_iterator it = paths_.begin(); it != paths_.end(); ++it) {
    it->second->set_id(-1);
  }
}

bool State::Deserialize(FILE* fp) {
  METRIC_RECORD("deserialize");
  string buf;

  vector<Pool*> pools;
  int pool_size = DeserializeInt(fp);
  if (pool_size < 0)
    return false;

  {
    METRIC_RECORD("deserialize pool");
    for (int i = 0; i < pool_size; i++) {
      if (!DeserializeString(fp, &buf))
        return false;
      int depth = DeserializeInt(fp);
      if (depth < 0)
        return false;

      Pool* pool = new Pool(buf, depth);
      pools.push_back(pool);
    }
    fprintf(stderr, "pool ok\n");
  }

#if 0
  fprintf(stderr, "top binding=%p\n", &bindings_);
  if (!bindings_.Deserialize(fp))
    return false;
#endif

  vector<BindingEnv*> bindings;
  int bindings_size = DeserializeInt(fp);
  if (bindings_size < 0)
    return false;
  {
    METRIC_RECORD("deserialize bindings");
    for (int i = 0; i < bindings_size; i++) {
      BindingEnv* b = i ? new BindingEnv() : &bindings_;
      if (!b->Deserialize(fp))
        return false;
      bindings.push_back(b);
    }
    for (int i = 0; i < bindings_size; i++) {
      int parent_id = DeserializeInt(fp);
      if (parent_id < 0)
        return false;
      if (parent_id) {
        if (parent_id > bindings.size()) {
          fprintf(stderr, "parent size overflow %d vs %zu\n", parent_id, bindings.size());
          abort();
        }
        bindings[i]->set_parent(bindings[parent_id-1]);
        //fprintf(stderr, "id=%d parent_id=%d parent=%p\n", i, parent_id-1, parent);
      }
    }
    fprintf(stderr, "bindings ok\n");
  }

  vector<Node*> nodes;
  {
    METRIC_RECORD("deserialize nodes");
    int path_size = DeserializeInt(fp);
    if (path_size < 0)
      return false;
    for (int i = 0; i < path_size; ++i) {
      if (!DeserializeString(fp, &buf))
        return false;
      //fprintf(stderr, "node #%d %s\n", i, buf.c_str());
      int slash_bits = DeserializeInt(fp);
      //fprintf(stderr, "node #%d %s %d\n", i, buf.c_str(), slash_bits);
      if (slash_bits < 0)
        return false;

      int node_id = static_cast<int>(nodes.size());
      Node* node = new Node(buf, slash_bits);
      node->set_id(node_id);
      nodes.push_back(node);
      if (!paths_.insert(make_pair(StringPiece(node->path()), node)).second)
        return false;
    }
    fprintf(stderr, "node ok\n");
  }

  int edge_size = DeserializeInt(fp);
  if (edge_size < 0)
    return false;
  vector<Rule*> rules;
  {
    METRIC_RECORD("deserialize edges");
    for (int i = 0; i < edge_size; ++i) {
      Edge* edge = new Edge();
      edges_.push_back(edge);

      int rule_id = DeserializeInt(fp);
      if (rule_id < 0)
        return false;

      //fprintf(stderr, "edge #%d rule_id=%d\n", i, rule_id);

      if (rule_id == 0) {
        edge->rule_ = &kPhonyRule;
      } else if (rule_id == 1) {
        // New rule.
        if (!DeserializeString(fp, &buf))
          return false;
        Rule* rule = new Rule(buf);
        edge->rule_ = rule;
        rules.push_back(rule);

        int binding_size = DeserializeInt(fp);
        if (binding_size < 0)
          return false;
        for (int j = 0; j < binding_size; j++) {
          if (!DeserializeString(fp, &buf))
            return false;
          EvalString es;
          if (!es.Deserialize(fp))
            return false;
          rule->AddBinding(buf, es);
        }
      } else {
        rule_id -= 2;
        if (rule_id >= static_cast<int>(rules.size())) {
          fprintf(stderr, "rule overflow %d vs %zu\n", rule_id, rules.size());
          return false;
        }
        edge->rule_ = rules[rule_id];
      }

      //fprintf(stderr, "edge #%d rule ok\n", i);

      int pool_id = DeserializeInt(fp);
      if (pool_id < 0)
        return false;
      if (pool_id >= static_cast<int>(pools.size()))
        return false;
      edge->pool_ = pools[pool_id];

      int input_size = DeserializeInt(fp);
      if (input_size < 0)
        return false;
      for (int j = 0; j < input_size; j++) {
        int input = DeserializeInt(fp);
        if (input < 0)
          return false;
        // TODO: check.
        Node* node = nodes[input];
        node->AddOutEdge(edge);
        edge->inputs_.push_back(node);
      }

      int output_size = DeserializeInt(fp);
      if (output_size < 0)
        return false;
      for (int j = 0; j < output_size; j++) {
        int output = DeserializeInt(fp);
        if (output < 0)
          return false;
        // TODO: check.
        Node* node = nodes[output];
        node->set_in_edge(edge);
        edge->outputs_.push_back(node);
      }

      int implicit_deps = DeserializeInt(fp);
      if (implicit_deps < 0)
        return false;
      edge->implicit_deps_ = implicit_deps;

      int order_only_deps = DeserializeInt(fp);
      if (order_only_deps < 0)
        return false;
      edge->order_only_deps_ = order_only_deps;

#if 0
      edge->env_ = new BindingEnv();
      fprintf(stderr, "binding=%p\n", edge->env_);
      if (!edge->env_->Deserialize(fp))
        return false;
#endif

      int binding_id = DeserializeInt(fp);
      if (binding_id < 0)
        return false;
      if (binding_id >= bindings.size())
        abort();
      edge->env_ = bindings[binding_id];
    }
  }
  fprintf(stderr, "edge & rule ok\n");

  int default_size = DeserializeInt(fp);
  if (default_size < 0)
    return false;
  for (int i = 0; i < default_size; ++i) {
    int node_id = DeserializeInt(fp);
    if (node_id < 0)
      return false;
    defaults_.push_back(nodes[node_id]);
  }

  for (Paths::const_iterator it = paths_.begin(); it != paths_.end(); ++it) {
    it->second->set_id(-1);
  }

  fprintf(stderr, "pools=%zu\n", pools_.size());
  fprintf(stderr, "edges=%zu\n", edges_.size());
  fprintf(stderr, "nodes=%zu\n", nodes.size());

  return true;
}
