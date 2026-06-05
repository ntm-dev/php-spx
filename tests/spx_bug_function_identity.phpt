--TEST--
function-table identity: no duplication (one function per entry) and no aliasing (distinct functions kept apart)
--INI--
spx.use_observer_api=0
--ENV--
return <<<END
SPX_ENABLED=1
SPX_BUILTINS=1
SPX_AUTO_START=1
SPX_REPORT=fp
SPX_METRICS=zuo
SPX_FP_FOCUS=zuo
END;
--FILE--
<?php
/*
 *  Regression guard for the function-table identity, which must avoid two opposite
 *  defects:
 *    - duplication: one logical function split across several entries;
 *    - aliasing: several distinct functions merged onto one entry.
 *
 *  Each scenario below covers one of these, described in the comment above it.
 *
 *  The output also lists three internal functions ("::zend_compile_file",
 *  "::zend_compile_string", "::php_request_shutdown"): like ArrayObject::count, they
 *  only show up because SPX_BUILTINS=1 which is required for the case (2).
 */

class A extends ArrayObject {}
class B extends ArrayObject {}

// (1) recompilation (anti-duplication): each eval() recompiles a fresh function, but
// its identity is keyed on the call site. The three eval()s share one call site, so
// they aggregate into one entry.
for ($i = 0; $i < 3; $i++) {
    eval('return 1;');
}

// (2) inheritance (anti-duplication): ArrayObject::count is an internal method
// inherited by A and B, it is copied into each child while keeping its defining
// scope, so reached through A and B it must aggregate into one entry. Being internal,
// it is only profiled with SPX_BUILTINS=1 (set in --ENV-- above).
(new A())->count();
(new B())->count();

// (3) closures (anti-aliasing): before PHP 8.4 every closure is named "{closure}",
// so $c1 and $c2 differ only by their definition site. They must stay on one entry each.
$c1 = function () { return 1; };
$c2 = function () { return 2; };
$c1();
$c2();
?>
--EXPECTF--
%a
Flat profile:

%s
%s
%s
 %s | %s | %s | ::zend_compile_file
 %s | %s | %s | ::zend_compile_string
 %s | %s | %s | %s/spx_bug_function_identity.php
 %s | %s | %s | %s/spx_bug_function_identity.php(%d) : eval()'d code
 %s | %s | %s | ArrayObject::__construct
 %s | %s | %s | ArrayObject::count
 %s | %s | %s | {closure:%s/spx_bug_function_identity.php:%d}
 %s | %s | %s | {closure:%s/spx_bug_function_identity.php:%d}
 %s | %s | %s | ::php_request_shutdown
