/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file graph_runtime.c
 * \brief implement graph runtime in pure C
 */
#include "graph_runtime.h"

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif  // MAX

uint32_t Shape_Accumulate(int64_t * shape, uint32_t ndim) {
  int64_t accum = 1;
  uint32_t idx;
  for (idx = 0; idx < ndim; idx++) {
    if (shape[idx] == 0) { break; }
    accum *= shape[idx];
  }
  return accum;
}

int NodeEntry_Load(TVMGraphRuntimeNodeEntry * entry, JSONReader * reader) {
  int status = 0;
  reader->BeginArray(reader);
  if (!(reader->NextArrayItem(reader))) {
    fprintf(stderr, "invalid json format: failed to parse `node_id`\n");
  }
  reader->ReadUnsignedInteger(reader, &(entry->node_id));
  if (!(reader->NextArrayItem(reader))) {
    fprintf(stderr, "invalid json format: failed to parse `index`\n");
  }
  reader->ReadUnsignedInteger(reader, &(entry->index));
  if (reader->NextArrayItem(reader)) {
    reader->ReadUnsignedInteger(reader, &(entry->version));
    if (reader->NextArrayItem(reader)) {
      fprintf(stderr, "invalid json format: failed to parse `version`\n");
    }
  } else {
    entry->version = 0;
  }
  return status;
}

void TVMGraphRuntimeNode_LoadAttrs(TVMGraphRuntimeNode * node, JSONReader *reader,
                                              TVMOpParam* param) {
  int bitmask = 0;
  char key[20], value[120];
  memset(param, 0, sizeof(TVMOpParam));
  memset(key, 0, sizeof(key));
  memset(value, 0, sizeof(value));
  reader->BeginObject(reader);
  while (reader->NextObjectItem(reader, key)) {
    reader->ReadString(reader, value);
    if (!strcmp(key, "func_name")) {
      snprintf(param->func_name, sizeof(value), "%s", value);
      bitmask |= 1;
    } else if (!strcmp(key, "num_inputs")) {
      param->num_inputs = strtoul(value, 0, 10);
      bitmask |= 2;
    } else if (!strcmp(key, "num_outputs")) {
      param->num_outputs = strtoul(value, 0, 10);
      bitmask |= 4;
    } else if (!strcmp(key, "flatten_data")) {
      param->flatten_data = strtoul(value, 0, 10);
      bitmask |= 8;
    } else {
      fprintf(stderr, "do not support key %s", key);
    }
  }
  if (bitmask != (1|2|4|8)) { fprintf(stderr, "invalid format\n"); }
}

int TVMGraphRuntimeNode_Load(TVMGraphRuntimeNode * node, JSONReader *reader) {
  int status = 0;
  reader->BeginObject(reader);
  int bitmask = 0;
  char key[20];
  while (reader->NextObjectItem(reader, key)) {
    if (!strcmp(key, "op")) {
      reader->ReadString(reader, node->op_type);
      bitmask |= 1;
    } else if (!strcmp(key, "name")) {
      reader->ReadString(reader, node->name);
      bitmask |= 2;
    } else if (!strcmp(key, "inputs")) {
      size_t count = node->inputs_count;
      if (count >= GRAPH_RUNTIME_NODE_MAX_INPUTS) {
        fprintf(stderr, "The number of inputs in graph runtime node is greater than expected.\n");
        status = -1;
        break;
      }
      reader->BeginArray(reader);
      while (reader->NextArrayItem(reader)) {
        TVMGraphRuntimeNodeEntry * inputs = node->inputs + count;
        reader->BeginArray(reader);
        if (!reader->NextArrayItem(reader)) {
          fprintf(stderr, "invalid json format\n");
          status = -1;
          break;
        }
        reader->ReadUnsignedInteger(reader, &(inputs->node_id));
        if (!reader->NextArrayItem(reader)) {
          fprintf(stderr, "invalid json format\n");
          status = -1;
          break;
        }
        reader->ReadUnsignedInteger(reader, &(inputs->index));
        if (reader->NextArrayItem(reader)) {
          reader->ReadUnsignedInteger(reader, &(inputs->version));
          if (reader->NextArrayItem(reader)) {
            fprintf(stderr, "invalid json format\n");
            status = -1;
            break;
          }
        } else {
          inputs->version = 0;
        }
        count++;
      }
      node->inputs_count = count;
      bitmask |= 4;
    } else if (!strcmp(key, "attr") || !strcmp(key, "attrs")) {
      TVMOpParam param;

      TVMGraphRuntimeNode_LoadAttrs(node, reader, &param);
      memcpy(&node->param, &param, sizeof(param));
    } else if (!strcmp(key, "control_deps")) {
      fprintf(stderr, "do not support key %s", key);
      status = -1;
    } else {
      fprintf(stderr, "do not support key %s", key);
      status = -1;
    }
    if (status != 0) { break; }
  }
  if (bitmask != (1|2|4)) { fprintf(stderr, "invalid format\n"); }
  return status;
}

TVMGraphRuntimeNode TVMGraphRuntimeNodeCreate() {
  TVMGraphRuntimeNode node;
  memset(&node, 0, sizeof(TVMGraphRuntimeNode));
  node.LoadAttrs = TVMGraphRuntimeNode_LoadAttrs;
  node.Load = TVMGraphRuntimeNode_Load;
  return node;
}

int TVMGraphRuntimeGraphAttr_Load(TVMGraphRuntimeGraphAttr * attr, JSONReader *reader) {
  int status = 0;
  int bitmask = 0;
  char key[16], type[16];
  uint32_t storage_id_count = 0;
  uint32_t dltype_count = 0;
  uint32_t shape_count = 0;
  uint32_t device_index_count = 0;
  reader->BeginObject(reader);
  while (reader->NextObjectItem(reader, key)) {
    if (!strcmp(key, "dltype")) {
      reader->BeginArray(reader);
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->ReadString(reader, type);
      if (strcmp(type, "list_str")) { fprintf(stderr, "Invalid json format\n"); }
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->BeginArray(reader);
      while (reader->NextArrayItem(reader)) {
        reader->ReadString(reader, attr->dltype[dltype_count]);
        dltype_count++;
      }
      attr->dltype_count = dltype_count;;
      if (reader->NextArrayItem(reader)) { fprintf(stderr, "Invalid json format\n"); }
      bitmask |= 1;
    } else if (!strcmp(key, "storage_id")) {
      reader->BeginArray(reader);
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->ReadString(reader, type);
      if (strcmp(type, "list_int")) { fprintf(stderr, "Invalid json format\n"); }
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->BeginArray(reader);
      while (reader->NextArrayItem(reader)) {
        reader->ReadUnsignedInteger(reader, &(attr->storage_id[storage_id_count]));
        storage_id_count++;
      }
      if (reader->NextArrayItem(reader)) { fprintf(stderr, "Invalid json format\n"); }
      bitmask |= 2;
    } else if (!strcmp(key, "shape")) {
      reader->BeginArray(reader);
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->ReadString(reader, type);
      if (strcmp(type, "list_shape")) { fprintf(stderr, "Invalid json format\n"); }
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->BeginArray(reader);
      while (reader->NextArrayItem(reader)) {
        reader->BeginArray(reader);
        reader->ReadInteger(reader, &(attr->shape[shape_count][0]));
        uint32_t ndim = 1;
        if (reader->NextArrayItem(reader)) {
          if (reader->NextArrayItem(reader)) {
            reader->ReadInteger(reader, &(attr->shape[shape_count][1])); ndim++;
            if (reader->NextArrayItem(reader)) {
              reader->ReadInteger(reader, &(attr->shape[shape_count][2])); ndim++;
              if (reader->NextArrayItem(reader)) {
                reader->ReadInteger(reader, &(attr->shape[shape_count][3])); ndim++;
                if (reader->NextArrayItem(reader)) {
                  reader->ReadInteger(reader, &(attr->shape[shape_count][4])); ndim++;
                  if (reader->NextArrayItem(reader)) {
                    reader->ReadInteger(reader, &(attr->shape[shape_count][5])); ndim++;
                    reader->NextArrayItem(reader);
                  }
                }
              }
            }
          }
        }
        attr->ndim[shape_count] = ndim;
        shape_count++;
      }
      attr->shape_count = shape_count;
      if (reader->NextArrayItem(reader)) { fprintf(stderr, "Invalid json format\n"); }
      bitmask |= 4;
    } else if (!strcmp(key, "device_index")) {
      reader->BeginArray(reader);
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->ReadString(reader, type);
      if (strcmp(type, "list_int")) { fprintf(stderr, "Invalid json format\n"); }
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      while (reader->NextArrayItem(reader)) {
        reader->ReadUnsignedInteger(reader, &(attr->device_index[device_index_count]));
        device_index_count++;
      }
      if (reader->NextArrayItem(reader)) { fprintf(stderr, "Invalid json format\n"); }
    } else {
      reader->BeginArray(reader);
      if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
      reader->ReadString(reader, type);
      if (!strcmp(type, "list_int")) {
        if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
        uint32_t temp[GRAPH_RUNTIME_MAX_NODES];
        uint32_t temp_count = 0;
        reader->BeginArray(reader);
        while (reader->NextArrayItem(reader)) {
          reader->ReadUnsignedInteger(reader, &(temp[temp_count]));
          temp_count++;
        }
      } else if (!strcmp(type, "size_t")) {
        if (!(reader->NextArrayItem(reader))) { fprintf(stderr, "Invalid json format\n"); }
        uint32_t temp;
        reader->ReadUnsignedInteger(reader, &temp);
      } else {
        fprintf(stderr, "cannot skip graph attr %s", key);
      }
      if (reader->NextArrayItem(reader)) { fprintf(stderr, "Invalid json format\n"); }
    }
  }
  if (bitmask != (1|2|4)) { fprintf(stderr, "invalid format\n"); }
  return status;
}

int TVMGraphRuntime_Load(TVMGraphRuntime * runtime, JSONReader *reader) {
    int status = 0;
    reader->BeginObject(reader);
    int bitmask = 0;
    char key[20];
    while (reader->NextObjectItem(reader, key)) {
      if (!strcmp(key, "nodes")) {
        reader->BeginArray(reader);
        while (reader->NextArrayItem(reader)) {
          TVMGraphRuntimeNode * node = runtime->nodes + runtime->nodes_count;
          status = TVMGraphRuntimeNode_Load(node, reader);
          if (status != 0) {
            fprintf(stderr, "failed to load an element in `nodes` field in graph runtime node.\n");
            break;
#if TVM_CRT_DEBUG
          } else {
            printf("layer %u: `%s` loaded.\n", runtime->nodes_count, node->name);
#endif  // TVM_CRT_DEBUG
          }
          runtime->nodes_count++;
        }
        bitmask |= 1;
      } else if (!strcmp(key, "arg_nodes")) {
        reader->BeginArray(reader);
        while (reader->NextArrayItem(reader)) {
          uint32_t * node = runtime->input_nodes + runtime->input_nodes_count;
          reader->ReadUnsignedInteger(reader, node);
          runtime->input_nodes_count++;
        }
        bitmask |= 2;
      } else if (!strcmp(key, "node_row_ptr")) {
        reader->BeginArray(reader);
        while (reader->NextArrayItem(reader)) {
          uint32_t count = runtime->node_row_ptr_count;
          uint32_t * node = runtime->node_row_ptr + count;
          reader->ReadUnsignedInteger(reader, node);
          runtime->node_row_ptr_count++;
        }
        bitmask |= 4;
      } else if (!strcmp(key, "heads")) {
        reader->BeginArray(reader);
        while (reader->NextArrayItem(reader)) {
          TVMGraphRuntimeNodeEntry * entry = runtime->outputs + runtime->outputs_count;
          status = NodeEntry_Load(entry, reader);
          if (status != 0) {
            fprintf(stderr, "Fail to load an element in `heads` field in graph runtime node.\n");
            break;
          }
          runtime->outputs_count++;
        }
        bitmask |= 8;
      } else if (!strcmp(key, "attrs")) {
        status = TVMGraphRuntimeGraphAttr_Load(&(runtime->attrs), reader);
        if (status != 0) {
          fprintf(stderr, "Fail to load an element in `heads` field in graph runtime node.\n");
          break;
        }
        bitmask |= 16;
      } else if (!strcmp(key, "metadata")) {
        break;
      } else {
        fprintf(stderr, "key %s is not supported\n", key);
        status = -1;
      }
      if (status != 0) { break; }
    }
    if (!(bitmask == (1|2|4|8|16))) { fprintf(stderr, "invalid format\n"); }
    return status;
}

uint32_t TVMGraphRuntime_GetEntryId(TVMGraphRuntime * runtime,
                                                  uint32_t nid, uint32_t index) {
  return runtime->node_row_ptr[nid] + index;
}

/*!
 * \brief Get the input index given the name of input.
 * \param name The name of the input.
 * \return The index of input.
 */
int TVMGraphRuntime_GetInputIndex(TVMGraphRuntime * runtime, const char * name) {
  uint32_t i;
  int32_t rv = -1;
  for (i = 0; i< runtime->input_nodes_count; ++i) {
    uint32_t nid = runtime->input_nodes[i];
    if (!strcmp(runtime->nodes[nid].name, name)) {
      rv = i;
      break;
    }
  }
  if (rv < 0) {
    fprintf(stderr, "cannot find \"%s\" among input\n", name);
  }
  return rv;
}

/*!
 * \brief set index-th input to the graph.
 * \param index The input index.
 * \param data_in The input data.
 */
void TVMGraphRuntime_SetInput(TVMGraphRuntime * runtime, const char * name, DLTensor* data_in) {
  uint32_t index = runtime->GetInputIndex(runtime, name);
  if (index >= runtime->input_nodes_count) {
    fprintf(stderr, "given index is greater than num of input nodes.\n");
  }
  uint32_t eid = runtime->GetEntryId(runtime, runtime->input_nodes[index], 0);
  runtime->data_entry[eid].dl_tensor = *data_in;
}

int TVMGraphRuntime_LoadParams(TVMGraphRuntime * runtime, const char * param_blob,
                               const uint32_t param_size) {
  int status = 0;
  const char * bptr = param_blob;
  uint64_t header, reserved;
  header = ((uint64_t*)bptr)[0];  // NOLINT(*)
  bptr += sizeof(header);
  if (header != kTVMNDArrayListMagic) {
    fprintf(stderr, "Invalid parameters file format");
  }
  reserved = ((uint64_t*)bptr)[0];  // NOLINT(*)
  bptr += sizeof(reserved);

  // read names
  char names[GRAPH_RUNTIME_MAX_NODES][80];
  memset(names, 0, sizeof(names));
  uint64_t names_count;
  int idx;
  names_count = ((uint64_t*)bptr)[0];  // NOLINT(*)
  bptr += sizeof(names_count);
  for (idx = 0; idx < names_count; idx++) {
    uint64_t name_length;
    name_length = ((uint64_t*)bptr)[0];  // NOLINT(*)
    bptr += sizeof(name_length);
    if (name_length >= 80) {
      fprintf(stderr, "Error: function name longer than expected.\n");
    }
    memcpy(names[idx], bptr, name_length);
    bptr += name_length;
  }

  // read sizes
  uint64_t sz;
  sz = ((uint64_t*)bptr)[0];  // NOLINT(*)
  bptr += sizeof(sz);
  uint32_t size = sz;
  if (size != names_count) {
    fprintf(stderr, "Invalid parameters file format\n");
    status = -1;
  }

  for (idx = 0; idx < size; idx++) {
    int32_t in_idx = runtime->GetInputIndex(runtime, names[idx]);
    if (!(in_idx >= 0)) {
      fprintf(stderr, "Found param for non-existent input: %s\n", names[idx]);
      status = -1;
    }
    uint32_t eid = runtime->GetEntryId(runtime, runtime->input_nodes[in_idx], 0);
    if (!(eid < runtime->data_entry_count)) {
      fprintf(stderr, "`entry_id`=%d is greater than expected(%d).\n",
              eid, runtime->data_entry_count);
      status = -1;
    }

    status |= TVMNDArray_Load(&(runtime->data_entry[eid]), &bptr);
#if TVM_CRT_DEBUG
    TVMNDArray * entry = &(runtime->data_entry[eid]);
    printf("param %s loaded, in_idx=%d, eid=%d, ndim=%d, data[0]=%f\n",
           names[idx], in_idx, eid, entry->dl_tensor.ndim,
           ((float*)entry->dl_tensor.data)[0]);  // NOLINT(*)
#endif  // TVM_CRT_DEBUG
  }

  return status;
}

/*!
 * \brief Run all the operations one by one.
 */
void TVMGraphRuntime_Run(TVMGraphRuntime * runtime) {
  // setup the array and requirements.
  uint32_t idx;
  for (idx = 0; idx < runtime->op_execs_count; ++idx) {
    if (runtime->op_execs[idx].fexec) {
#if TVM_CRT_DEBUG
      printf("calling %s (%d)\n", runtime->op_execs[idx].name, idx);
#endif  // TVM_CRT_DEBUG
      runtime->op_execs[idx].Call(&(runtime->op_execs[idx]));
    }
  }
}

int TVMGraphRuntime_GetOutput(TVMGraphRuntime * runtime, const int32_t idx, DLTensor * out) {
  int status = 0;
  uint32_t nid = runtime->outputs[idx].node_id;
  uint32_t index = runtime->outputs[idx].index;
  uint32_t eid = runtime->GetEntryId(runtime, nid, index);

  // copy data section to allocated output tensor
  int32_t elem_bytes = out->dtype.bits / 8;
  int64_t size = Shape_Accumulate(out->shape, out->ndim);
  DLTensor * tensor = &(runtime->data_entry[eid].dl_tensor);
  assert(out->ndim == tensor->ndim);
  assert(out->dtype.bits == tensor->dtype.bits);
  assert(Shape_Accumulate(out->shape, out->ndim) == Shape_Accumulate(tensor->shape, tensor->ndim));
  memcpy(out->data, tensor->data, size * elem_bytes);
  return status;
}

void TVMGraphRuntime_SetupStorage(TVMGraphRuntime * runtime) {
  uint32_t idx, dim;

  // Grab saved optimization plan from graph.
  DLDataType vtype[GRAPH_RUNTIME_MAX_NODES];
  TVMGraphRuntimeGraphAttr * attrs = &(runtime->attrs);
  for (idx = 0; idx < attrs->dltype_count; idx++) {
    vtype[idx] = String2DLDataType(attrs->dltype[idx]);
  }

  // Size and device type of each storage pool entry.
  TVMGraphRuntimePoolEntry pool_entry[GRAPH_RUNTIME_MAX_NODES];
  memset(pool_entry, 0, sizeof(pool_entry));
  uint32_t  pool_entry_count = 0;
  // Find the maximum space size.
  for (idx = 0; idx < attrs->shape_count; idx++) {
    int storage_id = attrs->storage_id[idx];
    // Use the fallback device if no device index is available.
    int device_type = runtime->ctxs[0].device_type;
    uint32_t size = Shape_Accumulate(attrs->shape[idx], attrs->ndim[idx]);
    DLDataType t = vtype[idx];
    uint32_t bits = t.bits * t.lanes;
    size_t bytes = ((bits + 7U) / 8U) * size;

    uint32_t sid = storage_id;
    if (sid >= pool_entry_count) {
      pool_entry_count = sid + 1;
    }
    pool_entry[sid].size = MAX(pool_entry[sid].size, bytes);
    pool_entry[sid].device_type = device_type;
  }

  // Allocate the space.
  for (idx = 0; idx < pool_entry_count; idx++) {
    TVMGraphRuntimePoolEntry pit = pool_entry[idx];
    int64_t shape[TVM_CRT_MAX_NDIM] = {0, };
    TVMContext ctx = runtime->ctxs[0];
    DLDataType dtype = {kDLFloat, 32, 1};
    shape[0] = (pit.size + 3) / 4;
    runtime->storage_pool[runtime->storage_pool_count] = TVMNDArray_Empty(1, shape, dtype, ctx);
    if (runtime->storage_pool[runtime->storage_pool_count].dl_tensor.data == 0) {
      fprintf(stderr, "fail to create storage_pool with idx=%d\n", idx);
    }
    runtime->storage_pool_count++;
  }

  // Assign the pooled entries. A unified memory pool is used to simplifiy
  // memory assignment for each node entry. The allocated memory on each device
  // is mapped to this pool.
  runtime->data_entry_count = runtime->node_row_ptr[runtime->node_row_ptr_count - 1];
  for (idx = 0; idx < runtime->data_entry_count; ++idx) {
    size_t storage_id = attrs->storage_id[idx];
    assert(storage_id < runtime->storage_pool_count);
    runtime->data_entry[idx] =
      TVMNDArray_CreateView(&(runtime->storage_pool[storage_id]),
                         attrs->shape[idx], attrs->ndim[idx], vtype[idx]);
    if (runtime->data_entry[idx].dl_tensor.data == 0) {
      fprintf(stderr, "fail to create for node with idx=%d, storage_id=%d\n", idx, storage_id);
    }
  }
}

int TVMGraphRuntime_SetupOpExecs(TVMGraphRuntime * runtime) {
  int status = 0;
  uint32_t nid, idx;
  runtime->op_execs_count = runtime->nodes_count;
  for (nid = 0; nid < runtime->nodes_count; nid++) {
    const TVMGraphRuntimeNode * inode = runtime->nodes + nid;
    if (strcmp(inode->op_type, "null")) {
      DLTensorPtr args[GRAPH_RUNTIME_MAX_NODES];
      uint32_t args_count = 0;
      for (idx = 0; idx < inode->inputs_count; idx++) {
        const TVMGraphRuntimeNodeEntry * entry = inode->inputs + idx;
        uint32_t eid = runtime->GetEntryId(runtime, entry->node_id, entry->index);
        args[idx] = &(runtime->data_entry[eid].dl_tensor);
        args_count++;
      }
      for (idx = 0; idx < inode->param.num_outputs; idx++) {
        uint32_t eid = runtime->GetEntryId(runtime, nid, idx);
        args[args_count] = &(runtime->data_entry[eid].dl_tensor);
        args_count++;
      }
      if (strcmp(inode->op_type, "tvm_op")) {
        fprintf(stderr, "Can only take tvm_op as op\n"); status = -1;
        break;
      }
      if (args_count >= TVM_CRT_MAX_ARGS) {
        fprintf(stderr, "too many arguments: expected less than %d args, but got %d.\n",
                TVM_CRT_MAX_ARGS, args_count);
        status = -1;
        break;
      }
#if TVM_CRT_DEBUG
      printf("creating tvm_op: %s with node_id=%d\n", inode->param.func_name, nid);
#endif  // TVM_CRT_DEBUG
      TVMPackedFunc pf;
      runtime->CreateTVMOp(runtime, &(inode->param), args, args_count, inode->inputs_count, &pf);
      runtime->op_execs[nid] = pf;
    }
  }
  return status;
}

typedef struct TVMOpArgs {
  DLTensor args[TVM_CRT_MAX_ARGS];
  uint32_t args_count;
  TVMValue arg_values[TVM_CRT_MAX_ARGS];
  uint32_t arg_values_count;
  uint32_t arg_tcodes[TVM_CRT_MAX_ARGS];
  uint32_t arg_tcodes_count;
  int64_t  shape_data[TVM_CRT_MAX_ARGS];
  uint32_t shape_data_count;
} TVMOpArgs;

int32_t TVMGraphRuntime_CreateTVMOp(TVMGraphRuntime * runtime, const TVMOpParam * param,
                                    DLTensorPtr * args, const uint32_t args_count,
                                    uint32_t num_inputs, TVMPackedFunc * pf) {
  uint32_t idx;
  TVMOpArgs arg_ptr;
  memset(&arg_ptr, 0, sizeof(TVMOpArgs));
  arg_ptr.args_count = args_count;
  if (param->flatten_data) {
    arg_ptr.shape_data_count = arg_ptr.args_count;
  }
  for (idx = 0; idx < arg_ptr.args_count; ++idx) {
    TVMValue v;
    memset(&v, 0, sizeof(v));
    DLTensor * t = &(arg_ptr.args[idx]);
    /* v.v_handle = &((*args)[idx]); */
    v.v_handle = args[idx];
    arg_ptr.arg_values[idx] = v;
    arg_ptr.arg_values_count++;
    arg_ptr.arg_tcodes[idx] = kTVMNDArrayHandle;
    arg_ptr.arg_tcodes_count++;
    if (param->flatten_data) {
      arg_ptr.shape_data[idx] = Shape_Accumulate(t->shape, t->ndim);
      t->ndim = 1;
      t->shape[0] = arg_ptr.shape_data[idx];
    }
  }
  if (!strcmp(param->func_name, "__nop") || !strcmp(param->func_name, "__copy")) {
    fprintf(stderr, "%s function is not yet supported.", param->func_name);
  }

  runtime->module.GetFunction(param->func_name, pf);
  TVMArgs targs = TVMArgs_Create(arg_ptr.arg_values, arg_ptr.arg_tcodes, arg_ptr.arg_values_count);
  pf->SetArgs(pf, &targs);

  return 0;
}

/*!
 * \brief Initialize the graph executor with graph and context.
 * \param graph_json The execution graph.
 * \param module The module containing the compiled functions for the host
 * processor.
 * \param ctxs The context of the host and devices where graph nodes will be
 * executed on.
 */
void TVMGraphRuntime_Init(TVMGraphRuntime * runtime, const char * graph_json,
                          const TVMModule * module, const TVMContext * ctxs) {
  JSONReader reader = JSONReader_Create(graph_json);
  runtime->Load(runtime, &reader);
  runtime->ctxs[0] = ctxs[0];
  runtime->SetupStorage(runtime);
  runtime->SetupOpExecs(runtime);
  JSONReader_Release(&reader);
}

TVMGraphRuntime * TVMGraphRuntimeCreate(const char * sym_json,
                                        const TVMModule * m, const TVMContext * ctxs) {
  TVMGraphRuntime * runtime = (TVMGraphRuntime*)malloc(sizeof(TVMGraphRuntime));  // NOLINT(*)
  memset(runtime, 0, sizeof(TVMGraphRuntime));
  runtime->GetEntryId = TVMGraphRuntime_GetEntryId;
  runtime->GetInputIndex = TVMGraphRuntime_GetInputIndex;
  runtime->Init = TVMGraphRuntime_Init;
  runtime->Load = TVMGraphRuntime_Load;
  runtime->SetInput = TVMGraphRuntime_SetInput;
  runtime->LoadParams = TVMGraphRuntime_LoadParams;
  runtime->Run = TVMGraphRuntime_Run;
  runtime->GetOutput = TVMGraphRuntime_GetOutput;
  runtime->SetupStorage = TVMGraphRuntime_SetupStorage;
  runtime->SetupOpExecs = TVMGraphRuntime_SetupOpExecs;
  runtime->CreateTVMOp = TVMGraphRuntime_CreateTVMOp;
  runtime->module.GetFunction = TVMModule_GetFunction;
  // init
  runtime->Init(runtime, sym_json, m, ctxs);
  return runtime;
}

void TVMGraphRuntimeRelease(TVMGraphRuntime ** pptr) {
  int32_t idx;
  TVMGraphRuntime * runtime = *pptr;
  for (idx = 0; idx < runtime->storage_pool_count; ++idx) {
    TVMNDArray_Release(&(runtime->storage_pool[idx]));
  }
  free(*pptr);
}
