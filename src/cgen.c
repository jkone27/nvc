//
//  Copyright (C) 2011-2014  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "util.h"
#include "phase.h"
#include "lib.h"
#include "common.h"
#include "vcode.h"
#include "rt/rt.h"
#include "rt/cover.h"

#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/Transforms/IPO.h>

#undef NDEBUG
#include <assert.h>

typedef struct {
   LLVMValueRef      *regs;
   LLVMBasicBlockRef *blocks;
   LLVMValueRef       fn;
} cgen_ctx_t;

static LLVMModuleRef  module = NULL;
static LLVMBuilderRef builder = NULL;
static LLVMValueRef   mod_name = NULL;

//static LLVMValueRef cgen_support_fn(const char *name);

#if 0
static LLVMValueRef llvm_int1(bool b)
{
   return LLVMConstInt(LLVMInt1Type(), b, false);
}
#endif

static LLVMValueRef llvm_int8(int8_t i)
{
   return LLVMConstInt(LLVMInt8Type(), i, false);
}

#if 0
static LLVMValueRef llvm_int32(int32_t i)
{
   return LLVMConstInt(LLVMInt32Type(), i, false);
}
#endif

#if 0
static LLVMValueRef llvm_int64(int64_t i)
{
   return LLVMConstInt(LLVMInt64Type(), i, false);
}
#endif

#if 0
static LLVMValueRef llvm_real(double r)
{
   return LLVMConstReal(LLVMDoubleType(), r);
}
#endif

static LLVMTypeRef llvm_void_ptr(void)
{
   return LLVMPointerType(LLVMInt8Type(), 0);
}

#if 0
static LLVMValueRef llvm_void_cast(LLVMValueRef ptr)
{
   return LLVMBuildPointerCast(builder, ptr, llvm_void_ptr(), "");
}
#endif

#if 0
static LLVMValueRef llvm_sizeof(LLVMTypeRef type)
{
   return LLVMBuildIntCast(builder, LLVMSizeOf(type),
                           LLVMInt32Type(), "");
}
#endif

#if 0
static LLVMValueRef llvm_fn(const char *name)
{
   LLVMValueRef fn = LLVMGetNamedFunction(module, name);
   if ((fn == NULL) && ((fn = cgen_support_fn(name)) == NULL))
      fatal("cannot find named function %s", name);
   return fn;
}
#endif

static void llvm_str(LLVMValueRef *chars, size_t n, const char *str)
{
   for (size_t i = 0; i < n; i++)
      chars[i] = llvm_int8(*str ? *(str++) : '\0');
}

#if 0
static LLVMTypeRef llvm_uarray_type(LLVMTypeRef base, int dims)
{
   // Unconstrained arrays are represented by a structure
   // containing the left and right indices, a flag indicating
   // direction, and a pointer to the array data

   LLVMTypeRef dim_fields[] = {
      LLVMInt32Type(),      // Left
      LLVMInt32Type(),      // Right
      LLVMInt8Type()        // Direction
   };

   LLVMTypeRef dim_struct =
      LLVMStructType(dim_fields, ARRAY_LEN(dim_fields), false);

   LLVMTypeRef fields[] = {
      LLVMPointerType(base, 0),
      LLVMArrayType(dim_struct, dims)
   };

   return LLVMStructType(fields, ARRAY_LEN(fields), false);
}
#endif

static LLVMTypeRef cgen_type(vcode_type_t type)
{
   switch (vtype_kind(type)) {
   case VCODE_TYPE_INT:
      {
         const uint64_t ulow = llabs(vtype_low(type)) - 1;
         const uint64_t high = vtype_high(type);

         const uint64_t umax = MAX(ulow, high);

         if (umax <= 1)
            return LLVMInt1Type();
         if (umax <= INT8_MAX)
            return LLVMInt8Type();
         else if (umax <= INT16_MAX)
            return LLVMInt16Type();
         else if (umax <= INT32_MAX)
            return LLVMInt32Type();
         else
            return LLVMInt64Type();
      }
      break;

   default:
      fatal("cannot convert vcode type %d to LLVM", vtype_kind(type));
   }
}

static const char *cgen_reg_name(vcode_reg_t r)
{
   static char buf[32];
   checked_sprintf(buf, sizeof(buf), "r%d", r);
   return buf;
}

static void cgen_op_return(int i)
{
   if (vcode_count_args(i) > 0) {
      assert(false);
   }
   else
      LLVMBuildRetVoid(builder);
}

static void cgen_op_jump(int i, cgen_ctx_t *ctx)
{
   LLVMBuildBr(builder, ctx->blocks[vcode_get_target(i)]);
}

static void cgen_op_fcall(int i, cgen_ctx_t *ctx)
{
   ident_t func = vcode_get_func(i);

   vcode_reg_t result = vcode_get_result(i);

   LLVMValueRef fn = LLVMGetNamedFunction(module, istr(func));
   if (fn == NULL) {
      fn = LLVMAddFunction(
         module,
         istr(func),
         LLVMFunctionType(cgen_type(vcode_reg_type(result)), NULL, 0, false));

      LLVMAddFunctionAttr(fn, LLVMNoUnwindAttribute);
   }

   ctx->regs[result] = LLVMBuildCall(builder, fn, NULL, 0,
                                     cgen_reg_name(result));
}

static void cgen_op_const(int i, cgen_ctx_t *ctx)
{

}

static void cgen_op_cmp(int i, cgen_ctx_t *ctx)
{

}

static void cgen_op_assert(int i, cgen_ctx_t *ctx)
{

}

static void cgen_op_wait(int i, cgen_ctx_t *ctx)
{
   LLVMBuildRetVoid(builder);
}

static void cgen_op(int i, cgen_ctx_t *ctx)
{
   const vcode_op_t op = vcode_get_op(i);
   switch (op) {
   case VCODE_OP_RETURN:
      cgen_op_return(i);
      break;
   case VCODE_OP_JUMP:
      cgen_op_jump(i, ctx);
      break;
   case VCODE_OP_FCALL:
      cgen_op_fcall(i, ctx);
      break;
   case VCODE_OP_CONST:
      cgen_op_const(i, ctx);
      break;
   case VCODE_OP_CMP:
      cgen_op_cmp(i, ctx);
      break;
   case VCODE_OP_ASSERT:
      cgen_op_assert(i, ctx);
      break;
   case VCODE_OP_WAIT:
      cgen_op_wait(i, ctx);
      break;
   default:
      fatal("cannot generate code for vcode op %s", vcode_op_string(op));
   }
}

static void cgen_block(int block, cgen_ctx_t *ctx)
{
   vcode_select_block(block);

   LLVMPositionBuilderAtEnd(builder, ctx->blocks[block]);

   const int nops = vcode_count_ops();
   for (int i = 0; i < nops; i++)
      cgen_op(i, ctx);
}

static void cgen_code(cgen_ctx_t *ctx)
{
   assert(ctx->regs == NULL);
   assert(ctx->blocks == NULL);

   const int nregs   = vcode_count_regs();
   const int nblocks = vcode_count_blocks();

   ctx->regs   = xcalloc(nregs * sizeof(LLVMValueRef));
   ctx->blocks = xcalloc(nblocks * sizeof(LLVMBasicBlockRef));

   for (int i = 0; i < nblocks; i++) {
      char *name = xasprintf("vcode_block_%d", i);
      ctx->blocks[i] = LLVMAppendBasicBlock(ctx->fn, name);
      free(name);
   }

   for (int i = 0; i < nblocks; i++)
      cgen_block(i, ctx);

   free(ctx->regs);
   free(ctx->blocks);
}

static void cgen_process(vcode_unit_t code)
{
   vcode_select_unit(code);

   LLVMTypeRef pargs[] = { LLVMInt32Type() };
   LLVMTypeRef ftype = LLVMFunctionType(LLVMVoidType(), pargs, 1, false);
   LLVMValueRef fn = LLVMAddFunction(module, istr(vcode_unit_name()), ftype);

   cgen_ctx_t ctx = {
      .fn = fn
   };
   cgen_code(&ctx);

#if 0

   cgen_nested_subprograms(t);

   cgen_ctx_t ctx = {
      .entry_list = NULL,
      .proc       = t
   };

   // Create a global structure to hold process state
   char *state_name = xasprintf("%s__state", istr(tree_ident(t)));
   LLVMTypeRef state_ty = cgen_process_state_type(t);
   ctx.state = LLVMAddGlobal(module, state_ty, state_name);
   LLVMSetLinkage(ctx.state, LLVMInternalLinkage);
   free(state_name);

   // Process state is initially undefined: call process function
   // with non-zero argument to initialise
   LLVMSetInitializer(ctx.state, LLVMGetUndef(state_ty));

   LLVMTypeRef pargs[] = { LLVMInt32Type() };
   LLVMTypeRef ftype = LLVMFunctionType(LLVMVoidType(), pargs, 1, false);
   ctx.fn = LLVMAddFunction(module, istr(tree_ident(t)), ftype);

   LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlock(ctx.fn, "entry");
   LLVMBasicBlockRef jt_bb    = LLVMAppendBasicBlock(ctx.fn, "jump_table");
   LLVMBasicBlockRef init_bb  = LLVMAppendBasicBlock(ctx.fn, "init");
   LLVMBasicBlockRef start_bb = LLVMAppendBasicBlock(ctx.fn, "start");

   LLVMPositionBuilderAtEnd(builder, entry_bb);

   // If the parameter is non-zero jump to the init block

   LLVMValueRef param = LLVMGetParam(ctx.fn, 0);
   LLVMValueRef reset =
      LLVMBuildICmp(builder, LLVMIntNE, param, llvm_int32(0), "");
   LLVMBuildCondBr(builder, reset, init_bb, jt_bb);

   // Generate the jump table at the start of a process to handle
   // resuming from a wait statement

   LLVMPositionBuilderAtEnd(builder, jt_bb);

   const int nstmts = tree_stmts(t);
   for (int i = 0; i < nstmts; i++)
      tree_visit(tree_stmt(t, i), cgen_jump_table_fn, &ctx);

   if (ctx.entry_list == NULL)
      warn_at(tree_loc(t), "no wait statement in process");

   cgen_jump_table(&ctx, start_bb);

   LLVMPositionBuilderAtEnd(builder, init_bb);

   // Variable initialisation

   cgen_proc_var_init(t, &ctx);

   // Driver initialisation

   tree_visit(t, cgen_driver_fn, &ctx);

   // Return to simulation kernel after initialisation

   LLVMValueRef state_ptr   = LLVMBuildStructGEP(builder, ctx.state, 0, "");
   LLVMValueRef context_ptr = LLVMBuildStructGEP(builder, ctx.state, 1, "");
   LLVMValueRef wait_ptr    = LLVMBuildStructGEP(builder, ctx.state, 2, "");

   cgen_sched_process(llvm_int64(0));
   LLVMBuildStore(builder, llvm_int32(0 /* start */), state_ptr);
   LLVMBuildStore(builder, LLVMConstNull(llvm_void_ptr()), context_ptr);
   LLVMBuildStore(builder, llvm_int64(0), wait_ptr);
   LLVMBuildRetVoid(builder);

   // Sequential statements

   LLVMPositionBuilderAtEnd(builder, start_bb);

   for (int i = 0; i < nstmts; i++)
      cgen_stmt(tree_stmt(t, i), &ctx);

   LLVMBuildBr(builder, start_bb);

   // Free context memory

   while (ctx.entry_list != NULL) {
      proc_entry_t *next = ctx.entry_list->next;
      free(ctx.entry_list);
      ctx.entry_list = next;
   }
#endif
}

static void cgen_reset_function(vcode_unit_t vcode)
{
   vcode_select_unit(vcode);

   char *name LOCAL = xasprintf("%s_reset", istr(vcode_unit_name()));
   LLVMValueRef fn =
      LLVMAddFunction(module, name,
                      LLVMFunctionType(LLVMVoidType(), NULL, 0, false));

   cgen_ctx_t ctx = {
      .fn = fn
   };
   cgen_code(&ctx);
}

static void cgen_coverage_state(tree_t t)
{
   const int stmt_tags = tree_attr_int(t, ident_new("stmt_tags"), 0);
   if (stmt_tags > 0) {
      LLVMTypeRef type = LLVMArrayType(LLVMInt32Type(), stmt_tags);
      LLVMValueRef var = LLVMAddGlobal(module, type, "cover_stmts");
      LLVMSetInitializer(var, LLVMGetUndef(type));
   }

   const int cond_tags = tree_attr_int(t, ident_new("cond_tags"), 0);
   if (cond_tags > 0) {
      LLVMTypeRef type = LLVMArrayType(LLVMInt32Type(), stmt_tags);
      LLVMValueRef var = LLVMAddGlobal(module, type, "cover_conds");
      LLVMSetInitializer(var, LLVMGetUndef(type));
   }
}

static void cgen_top(tree_t t)
{
   vcode_unit_t vcode = tree_code(t);

   cgen_coverage_state(t);

   cgen_reset_function(vcode);

   if (tree_kind(t) == T_ELAB) {
      const int nstmts = tree_stmts(t);
      for (int i = 0; i < nstmts; i++)
         cgen_process(tree_code(tree_stmt(t, i)));
   }
}

static void cgen_optimise(void)
{
   LLVMPassManagerRef pass_mgr = LLVMCreatePassManager();

   LLVMAddPromoteMemoryToRegisterPass(pass_mgr);
   LLVMAddInstructionCombiningPass(pass_mgr);
   LLVMAddReassociatePass(pass_mgr);
   LLVMAddGVNPass(pass_mgr);
   LLVMAddCFGSimplificationPass(pass_mgr);

   LLVMRunPassManager(pass_mgr, module);
   LLVMDisposePassManager(pass_mgr);
}

#if 0
static const char *cgen_memcpy_name(int width)
{
   static char name[64];
   checked_sprintf(name, sizeof(name),
                   "llvm.memcpy.p0i%d.p0i%d.i32", width, width);
   return name;
}
#endif

#if 0
static LLVMValueRef cgen_support_fn(const char *name)
{
   LLVMValueRef fn = NULL;
   if (strcmp(name, "_sched_process") == 0) {
      LLVMTypeRef args[] = { LLVMInt64Type() };
      fn = LLVMAddFunction(module, "_sched_process",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_sched_waveform") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMInt64Type(),
         LLVMInt64Type()
      };
      fn = LLVMAddFunction(module, "_sched_waveform",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_sched_event") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_sched_event",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_set_initial") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type(),
         llvm_void_ptr(),
         LLVMPointerType(LLVMInt32Type(), 0),
         LLVMInt32Type(),
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0)
      };
      fn = LLVMAddFunction(module, "_set_initial",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_needs_last_value") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(LLVMInt32Type(), 0),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_needs_last_value",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_resolved_address") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_resolved_address",
                           LLVMFunctionType(llvm_void_ptr(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_alloc_driver") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(LLVMInt32Type(), 0),
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt32Type(), 0),
         LLVMInt32Type(),
         llvm_void_ptr()
      };
      fn = LLVMAddFunction(module, "_alloc_driver",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_assert_fail") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(LLVMInt8Type(), 0),
         LLVMInt32Type(),
         LLVMInt8Type(),
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0)
      };
      fn = LLVMAddFunction(module, "_assert_fail",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_vec_load") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMInt1Type()
      };
      fn = LLVMAddFunction(module, "_vec_load",
                           LLVMFunctionType(llvm_void_ptr(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_image") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt64Type(),
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0),
         LLVMPointerType(llvm_uarray_type(LLVMInt8Type(), 1), 0)
      };
      fn = LLVMAddFunction(module, "_image",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_debug_out") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_debug_out",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_debug_dump") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_debug_dump",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "llvm.pow.f64") == 0) {
      LLVMTypeRef args[] = {
         LLVMDoubleType(),
         LLVMDoubleType()
      };
      fn = LLVMAddFunction(module, "llvm.pow.f64",
                           LLVMFunctionType(LLVMDoubleType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "llvm.memset.p0i8.i32") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(LLVMInt8Type(), 0),
         LLVMInt8Type(),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMInt1Type()
      };
      fn = LLVMAddFunction(module, "llvm.memset.p0i8.i32",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "llvm.expect.i1") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt1Type(),
         LLVMInt1Type()
      };
      fn = LLVMAddFunction(module, "llvm.expect.i1",
                           LLVMFunctionType(LLVMInt1Type(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strncmp(name, "llvm.memcpy", 11) == 0) {
      int width;
      if (sscanf(name, "llvm.memcpy.p0i%d", &width) != 1)
         fatal("invalid memcpy intrinsic %s", name);

      LLVMTypeRef args[] = {
         LLVMPointerType(LLVMIntType(width), 0),
         LLVMPointerType(LLVMIntType(width), 0),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMInt1Type()
      };
      fn = LLVMAddFunction(module, cgen_memcpy_name(width),
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_file_open") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(LLVMInt8Type(), 0),
         LLVMPointerType(llvm_void_ptr(), 0),
         LLVMPointerType(LLVMInt8Type(), 0),
         LLVMInt32Type(),
         LLVMInt8Type()
      };
      fn = LLVMAddFunction(module, "_file_open",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_file_write") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(llvm_void_ptr(), 0),
         llvm_void_ptr(),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_file_write",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_file_read") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(llvm_void_ptr(), 0),
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt32Type(), 0)
      };
      fn = LLVMAddFunction(module, "_file_read",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_file_close") == 0) {
      LLVMTypeRef args[] = {
         LLVMPointerType(llvm_void_ptr(), 0)
      };
      fn = LLVMAddFunction(module, "_file_close",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_endfile") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr()
      };
      fn = LLVMAddFunction(module, "_endfile",
                           LLVMFunctionType(LLVMInt1Type(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_bounds_fail") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_bounds_fail",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
      LLVMAddFunctionAttr(fn, LLVMNoReturnAttribute);
   }
   else if (strcmp(name, "_div_zero") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0)
      };
      fn = LLVMAddFunction(module, "_div_zero",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
      LLVMAddFunctionAttr(fn, LLVMNoReturnAttribute);
   }
   else if (strcmp(name, "_null_deref") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0)
      };
      fn = LLVMAddFunction(module, "_null_deref",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
      LLVMAddFunctionAttr(fn, LLVMNoReturnAttribute);
   }
   else if (strcmp(name, "_bit_shift") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt1Type(), 0),
         LLVMInt32Type(),
         LLVMInt8Type(),
         LLVMInt32Type(),
         LLVMPointerType(llvm_uarray_type(LLVMInt1Type(), 1), 0)
      };
      fn = LLVMAddFunction(module, "_bit_shift",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_bit_vec_op") == 0) {
      LLVMTypeRef args[] = {
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt1Type(), 0),
         LLVMInt32Type(),
         LLVMInt8Type(),
         LLVMPointerType(LLVMInt1Type(), 0),
         LLVMInt32Type(),
         LLVMInt8Type(),
         LLVMPointerType(llvm_uarray_type(LLVMInt1Type(), 1), 0)
      };
      fn = LLVMAddFunction(module, "_bit_vec_op",
                           LLVMFunctionType(LLVMVoidType(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_test_net_flag") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_test_net_flag",
                           LLVMFunctionType(LLVMInt1Type(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_last_event") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         LLVMInt32Type()
      };
      fn = LLVMAddFunction(module, "_last_event",
                           LLVMFunctionType(LLVMInt64Type(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_value_attr") == 0) {
      LLVMTypeRef args[] = {
         llvm_void_ptr(),
         LLVMInt32Type(),
         LLVMInt32Type(),
         LLVMPointerType(LLVMInt8Type(), 0)
      };
      fn = LLVMAddFunction(module, "_value_attr",
                           LLVMFunctionType(LLVMInt64Type(),
                                            args, ARRAY_LEN(args), false));
   }
   else if (strcmp(name, "_std_standard_now") == 0)
      fn = LLVMAddFunction(module, "_std_standard_now",
                           LLVMFunctionType(LLVMInt64Type(), NULL, 0, false));

   if (fn != NULL)
      LLVMAddFunctionAttr(fn, LLVMNoUnwindAttribute);

   return fn;
}
#endif

static void cgen_module_name(tree_t top)
{
   const char *name_str = istr(tree_ident(top));

   size_t len = strlen(name_str);
   LLVMValueRef chars[len + 1];
   llvm_str(chars, len + 1, name_str);

   mod_name = LLVMAddGlobal(module,
                            LLVMArrayType(LLVMInt8Type(), len + 1),
                            "module_name");
   LLVMSetInitializer(mod_name,
                      LLVMConstArray(LLVMInt8Type(), chars, len + 1));
   LLVMSetLinkage(mod_name, LLVMPrivateLinkage);
}

static void cgen_tmp_stack(void)
{
   LLVMValueRef _tmp_stack =
      LLVMAddGlobal(module, LLVMPointerType(llvm_void_ptr(), 0), "_tmp_stack");
   LLVMSetLinkage(_tmp_stack, LLVMExternalLinkage);

   LLVMValueRef _tmp_alloc =
      LLVMAddGlobal(module, LLVMInt32Type(), "_tmp_alloc");
   LLVMSetLinkage(_tmp_alloc, LLVMExternalLinkage);
}

void cgen(tree_t top)
{
   tree_kind_t kind = tree_kind(top);
   if (kind != T_ELAB && kind != T_PACK_BODY && kind != T_PACKAGE)
      fatal("cannot generate code for %s", tree_kind_str(kind));

   module = LLVMModuleCreateWithName(istr(tree_ident(top)));
   builder = LLVMCreateBuilder();

   cgen_module_name(top);
   cgen_tmp_stack();

   cgen_top(top);

   if (opt_get_int("dump-llvm"))
      LLVMDumpModule(module);

   if (LLVMVerifyModule(module, LLVMPrintMessageAction, NULL))
      fatal("LLVM verification failed");

   cgen_optimise();

   char *fname = xasprintf("_%s.bc", istr(tree_ident(top)));

   FILE *f = lib_fopen(lib_work(), fname, "w");
   if (LLVMWriteBitcodeToFD(module, fileno(f), 0, 0) != 0)
      fatal("error writing LLVM bitcode");
   fclose(f);
   free(fname);

   LLVMDisposeBuilder(builder);
   LLVMDisposeModule(module);
}
