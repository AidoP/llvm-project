import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        """Tests deferencing lvalue/rvalue references via LLDB's builtin type system."""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        # Take an lvalue reference and call `Dereference` on the SBValue.
        # The result should be `TTT` (and *not* for example the underlying type
        # 'int').
        lref_val = self.expect_var_path("l_ref", type="TTT &")
        self.assertEqual(lref_val.Dereference().GetType().GetName(), "TTT")

        # Same as above for rvalue references.
        rref_val = self.expect_var_path("r_ref", type="TTT &&")
        self.assertEqual(rref_val.Dereference().GetType().GetName(), "TTT")

        # Typedef to a reference should dereference to the underlying type.
        td_val = self.expect_var_path("td_to_ref_type", type="td_int_ref")
        self.assertEqual(td_val.Dereference().GetType().GetName(), "int")

    def test_take_address_of_reference(self):
        """Tests taking address of lvalue/rvalue references in lldb works correctly."""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        plref_val_from_code = self.expect_var_path("pl_ref", type="TTT *")
        plref_val_from_expr_path = self.expect_var_path("&l_ref", type="TTT *")
        self.assertEqual(
            plref_val_from_code.GetValueAsAddress(),
            plref_val_from_expr_path.GetValueAsAddress(),
        )

        prref_val_from_code = self.expect_var_path("pr_ref", type="TTT *")
        prref_val_from_expr_path = self.expect_var_path("&r_ref", type="TTT *")
        self.assertEqual(
            prref_val_from_code.GetValueAsAddress(),
            prref_val_from_expr_path.GetValueAsAddress(),
        )
