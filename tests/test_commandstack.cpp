// test_commandstack.cpp — generic undo/redo stack (Phase 13 / E7).
// Headless: commands mutate an int, no GL, no scene.
//
// Acceptance (plan doc 11 E7): do/undo/redo/merge/depth-overflow.

#include <doctest.h>

#include "core/CommandStack.h"

#include <memory>
#include <string>

using namespace Cosmic;

namespace
{
    // A command that sets an int from `before` to `after`. Do() applies `after`,
    // Undo() restores `before`. Optionally coalesces with same-key commands by
    // adopting the newer command's `after`.
    struct SetIntCommand : public ICommand
    {
        int*        Target;
        int         Before;
        int         After;
        std::string Key;

        SetIntCommand(int* t, int before, int after, std::string key = {})
            : Target(t), Before(before), After(after), Key(std::move(key)) {}

        void        Do() override   { *Target = After; }
        void        Undo() override { *Target = Before; }
        std::string Name() const override { return "Set Int"; }
        std::string MergeKey() const override { return Key; }

        bool TryMerge(const ICommand& next) override
        {
            const auto* n = dynamic_cast<const SetIntCommand*>(&next);
            if (!n || n->Key.empty() || n->Key != Key)
                return false;
            After = n->After;   // absorb the newer post-state; keep our Before
            return true;
        }
    };
}

TEST_CASE("E7: Execute applies the change and records one undo step")
{
    int v = 0;
    CommandStack stack;

    stack.Execute(std::make_unique<SetIntCommand>(&v, 0, 5));
    CHECK(v == 5);
    CHECK(stack.CanUndo());
    CHECK_FALSE(stack.CanRedo());
    CHECK(stack.UndoCount() == 1);
}

TEST_CASE("E7: Undo/Redo walk the history")
{
    int v = 0;
    CommandStack stack;

    stack.Execute(std::make_unique<SetIntCommand>(&v, 0, 1));
    stack.Execute(std::make_unique<SetIntCommand>(&v, 1, 2));
    stack.Execute(std::make_unique<SetIntCommand>(&v, 2, 3));
    CHECK(v == 3);

    CHECK(stack.Undo()); CHECK(v == 2);
    CHECK(stack.Undo()); CHECK(v == 1);
    CHECK(stack.Redo()); CHECK(v == 2);
    CHECK(stack.Redo()); CHECK(v == 3);
    CHECK_FALSE(stack.Redo());   // nothing left to redo
    CHECK(v == 3);
}

TEST_CASE("E7: a new command after Undo clears the redo branch")
{
    int v = 0;
    CommandStack stack;

    stack.Execute(std::make_unique<SetIntCommand>(&v, 0, 1));
    stack.Execute(std::make_unique<SetIntCommand>(&v, 1, 2));
    stack.Undo();                       // v == 1, one redo available
    CHECK(stack.CanRedo());

    stack.Execute(std::make_unique<SetIntCommand>(&v, 1, 9));
    CHECK(v == 9);
    CHECK_FALSE(stack.CanRedo());        // redo branch was discarded
}

TEST_CASE("E7: Push records without re-applying (already-live edit)")
{
    int v = 0;
    CommandStack stack;

    v = 7;                               // pretend an ImGui drag already set it
    stack.Push(std::make_unique<SetIntCommand>(&v, 0, 7));
    CHECK(v == 7);                       // Push must NOT re-run Do()

    CHECK(stack.Undo()); CHECK(v == 0);
    CHECK(stack.Redo()); CHECK(v == 7);  // Redo re-applies through Do()
}

TEST_CASE("E7: same-key commands coalesce into one undo step")
{
    int v = 0;
    CommandStack stack;

    // Simulate a continuous drag: many edits, same merge key.
    v = 1; stack.Push(std::make_unique<SetIntCommand>(&v, 0, 1, "drag"));
    v = 2; stack.Push(std::make_unique<SetIntCommand>(&v, 1, 2, "drag"));
    v = 3; stack.Push(std::make_unique<SetIntCommand>(&v, 2, 3, "drag"));

    CHECK(stack.UndoCount() == 1);       // merged
    CHECK(stack.Undo());
    CHECK(v == 0);                       // reverts to the very first "before"
    CHECK(stack.Redo());
    CHECK(v == 3);                       // redo restores the final "after"
}

TEST_CASE("E7: a merge barrier splits a coalescing run")
{
    int v = 0;
    CommandStack stack;

    v = 1; stack.Push(std::make_unique<SetIntCommand>(&v, 0, 1, "drag"));
    stack.SetMergeBarrier();             // user released the mouse
    v = 2; stack.Push(std::make_unique<SetIntCommand>(&v, 1, 2, "drag"));

    CHECK(stack.UndoCount() == 2);       // two separate steps despite same key
    CHECK(stack.Undo()); CHECK(v == 1);
    CHECK(stack.Undo()); CHECK(v == 0);
}

TEST_CASE("E7: empty merge keys never coalesce")
{
    int v = 0;
    CommandStack stack;

    v = 1; stack.Push(std::make_unique<SetIntCommand>(&v, 0, 1));   // no key
    v = 2; stack.Push(std::make_unique<SetIntCommand>(&v, 1, 2));   // no key
    CHECK(stack.UndoCount() == 2);
}

TEST_CASE("E7: history is bounded to the max depth (oldest dropped)")
{
    int v = 0;
    CommandStack stack(3);               // keep only the 3 newest

    for (int i = 0; i < 10; ++i)
        stack.Execute(std::make_unique<SetIntCommand>(&v, i, i + 1));
    CHECK(v == 10);
    CHECK(stack.UndoCount() == 3);

    // Only the last three are reversible: 10->9->8->7, then no more.
    CHECK(stack.Undo()); CHECK(v == 9);
    CHECK(stack.Undo()); CHECK(v == 8);
    CHECK(stack.Undo()); CHECK(v == 7);
    CHECK_FALSE(stack.Undo());
    CHECK(v == 7);
}

TEST_CASE("E7: the dirty callback fires on mutating ops only")
{
    int v = 0;
    int dirtyCount = 0;
    CommandStack stack;
    stack.SetDirtyCallback([&] { ++dirtyCount; });

    stack.Execute(std::make_unique<SetIntCommand>(&v, 0, 1));  // +1
    stack.Undo();                                              // +1
    stack.Redo();                                              // +1
    CHECK(dirtyCount == 3);

    dirtyCount = 0;
    stack.Clear();                                            // no fire
    CHECK(dirtyCount == 0);
    CHECK_FALSE(stack.CanUndo());
}
