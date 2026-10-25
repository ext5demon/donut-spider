#undef ENABLE_VM_TRACING
#include "../src/vm.c"
#include <assert.h>

void VMBuiltins_setVariable(VMContext* ctx, Instance* inst, int16_t id,
                           const char* name, RValue value, int32_t index) {
    (void)ctx; (void)inst; (void)id; (void)name; (void)value; (void)index;
    assert(!"A struct field must not write an instance builtin");
}

static void push(VMContext* ctx, int value, int type) {
    stackPushTyped(ctx, RValue_makeInt32(value), type);
}

int main(void) {
    DataWin dw = {0};
    dw.gen8.wadVersion = 17;
    VMContext ctx = {0};
    ctx.dataWin = &dw;
    ctx.currentCodeName = "stack regression";
    // Actual thrashfit show_menu operand: five arguments above new_option.
    // The second value is the unboxed result of 170 + ja_offset (Add.v.i).
    push(&ctx, 100001, GML_TYPE_VARIABLE);
    push(&ctx, 170, GML_TYPE_INT32);
    for (int i = 2; i <= 5; ++i) push(&ctx, i, GML_TYPE_VARIABLE);
    handleDup(&ctx, 0x86058805);
    assert(ctx.stack.top == 6);
    assert(ctx.stack.slots[0].int32 == 170);
    for (int i = 1; i <= 4; ++i) assert(ctx.stack.slots[i].int32 == i + 1);
    assert(ctx.stack.slots[5].int32 == 100001);
    handleDup(&ctx, 0x86050000);
    assert(ctx.stack.top == 7 && ctx.stack.slots[6].int32 == 100001);

    // Boxed call return consumed by a scalar dup; no adjacent argument stolen.
    ctx.stack.top = 0;
    push(&ctx, 41, GML_TYPE_INT32);
    push(&ctx, 42, GML_TYPE_VARIABLE);
    handleDup(&ctx, 0x86020000);
    assert(ctx.stack.top == 3 && ctx.stack.slots[2].int32 == 42);
    assert(bytesToSlotCount(&ctx, 20, 2) == 2);

    // Typed swap still counts bytes: two int32s above an eight-byte double.
    ctx.stack.top = 0;
    push(&ctx, 1, GML_TYPE_DOUBLE);
    push(&ctx, 2, GML_TYPE_INT32);
    push(&ctx, 3, GML_TYPE_INT32);
    handleDup(&ctx, 0x86029002);
    assert(ctx.stack.slots[0].int32 == 2 && ctx.stack.slots[1].int32 == 3);
    assert(ctx.stack.slots[2].int32 == 1);

    // Large rotation used to overrun a 32-value local array. Check every slot.
    ctx.stack.top = 0;
    for (int i = 0; i < 101; ++i) push(&ctx, i, GML_TYPE_VARIABLE);
    handleDup(&ctx, 0x86058864);
    for (int i = 0; i < 100; ++i) assert(ctx.stack.slots[i].int32 == i + 1);
    assert(ctx.stack.slots[100].int32 == 0);

    ctx.stack.top = 0;
    stackPushTyped(&ctx, RValue_makeOwnedString(safeStrdup("owned")), GML_TYPE_VARIABLE);
    push(&ctx, 7, GML_TYPE_VARIABLE);
    handleDup(&ctx, 0x86058801);
    handleDup(&ctx, 0x86050000);
    assert(ctx.stack.slots[1].string != ctx.stack.slots[2].string);
    assert(strcmp(ctx.stack.slots[1].string, ctx.stack.slots[2].string) == 0);
    for (int i = 0; i < ctx.stack.top; ++i) RValue_free(&ctx.stack.slots[i]);
    Instance* object = Instance_create(100123, STRUCT_OBJECT_INDEX, 0, 0);
    Variable field = {.varID = VARIABLE_BUILTIN, .name = "x"};
    ArrayAccess access = {.arrayIndex = -1};
    writeSingleInstanceVariable(&ctx, object, &field, &access, RValue_makeInt32(77));
    assert(VM_structGetVariableByVarName(&ctx, object, "x", -1).int32 == 77);
    assert(object->x == 0);
    Instance_freeContents(object); free(object);
    for (int i = 0; i < shlen(ctx.varNameMap); ++i) free(ctx.varNameMap[i].key);
    shfree(ctx.varNameMap);
    puts("PASS: outfit argument rotation, boxed returns, typed/large swaps and ownership");
    return 0;
}
