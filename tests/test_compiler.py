import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = [
    "src/entry.c",
    "src/cli/cli.c",
    "src/compiler/compiler.c",
    "src/lexer/lexer.c",
    "src/logger/logger.c",
]


class CompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_directory = tempfile.TemporaryDirectory(prefix="domus-build-")
        cls.compiler = pathlib.Path(cls.build_directory.name) / "domusc.exe"
        command = [
            "gcc",
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-Isrc",
            *SOURCES,
            "-o",
            str(cls.compiler),
        ]
        result = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, timeout=60
        )
        if result.returncode != 0:
            raise RuntimeError("Strict compiler build failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.build_directory.cleanup()

    def setUp(self):
        self.work_directory = tempfile.TemporaryDirectory(prefix="domus-test-")
        self.addCleanup(self.work_directory.cleanup)
        self.work = pathlib.Path(self.work_directory.name)

    def transpile(self, source, name="program", debug=False):
        source_path = self.work / (name + ".domus")
        c_path = self.work / (name + ".c")
        source_path.write_text(source, encoding="ascii")
        command = [
            str(self.compiler),
            "--in",
            str(source_path),
            "--out",
            str(c_path),
        ]
        if debug:
            command.insert(1, "--debug")
        result = subprocess.run(
            command, cwd=self.work, capture_output=True, text=True, timeout=10
        )
        return result, c_path

    def compile_generated(self, c_path):
        executable = self.work / "generated.exe"
        result = subprocess.run(
            [
                "gcc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                str(c_path),
                "-o",
                str(executable),
            ],
            cwd=self.work,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        return executable

    def assert_compiler_error(self, source, kind, line, name):
        result, c_path = self.transpile(source, name)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        module = "lexer" if kind == "lexico" else "compiler"
        self.assertIn("[{}/{}] na linha {}".format(module, kind, line), result.stderr)
        self.assertFalse(c_path.exists())

    def test_assignment2_transpiles_and_executes(self):
        result, c_path = self.transpile(
            (ROOT / "tests/assignment2.domus").read_text(encoding="ascii"),
            "assignment2",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Tabela de simbolos", result.stdout)
        for symbol in ("temperatura", "claridade", "cortina", "ventilador", "x", "w", "asd"):
            self.assertIn(symbol, result.stdout)

        source = c_path.read_text(encoding="ascii")
        self.assertIn("compiler_div_helper", source)
        self.assertIn("int domus_temperatura;", source)
        self.assertIn("int domus_ventilador;", source)
        self.assertNotIn("domus_ventiladora", source)

        executable = self.compile_generated(c_path)
        run = subprocess.run(
            [str(executable)], input="2\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "12\n")

    def test_default_input_transpiles_and_executes(self):
        result, c_path = self.transpile(
            (ROOT / "tests/new.domus").read_text(encoding="ascii"),
            "default",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        source = c_path.read_text(encoding="ascii")
        self.assertIn("int domus_ventilador;", source)
        self.assertNotIn("domus_ventiladora", source)
        executable = self.compile_generated(c_path)
        run = subprocess.run(
            [str(executable)], cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "12\n")

    def test_precedence_relations_branches_and_loop(self):
        source = """sensor temperatura:analogico(porta 1).
atuador cortina:digital(porta 2), motor:analogico(porta 3).
ler(x).
total <- 2 + 3 * 4.
ordem <- 20 / 5 / 2.
cortina <- x.
motor <- total.
se total > 0 entao se total == 14 entao escrever(total). senao escrever(0). fim_se. fim_se.
se 1 < 2 entao escrever(1). fim_se.
se 2 <= 2 entao escrever(1). fim_se.
se 3 > 2 entao escrever(1). fim_se.
se 3 >= 3 entao escrever(1). fim_se.
se 1 != 2 entao escrever(1). fim_se.
enquanto (x > 0) x <- x - 1. fim_enquanto.
escrever(ordem).
"""
        result, c_path = self.transpile(source, "expressions")
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = self.compile_generated(c_path)
        run = subprocess.run(
            [str(executable)], input="1\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.splitlines(), ["14", "1", "1", "1", "1", "1", "2"])

    def test_helpers_are_emitted_only_for_used_operations(self):
        cases = [
            ("value <- 1.\n", None, ("compiler_read_int", "compiler_add_helper", "compiler_subtract_helper", "compiler_multiply_helper", "compiler_div_helper")),
            ("value <- 1 + 2.\n", "compiler_add_helper", ("compiler_read_int", "compiler_subtract_helper", "compiler_multiply_helper", "compiler_div_helper")),
            ("value <- 2 - 1.\n", "compiler_subtract_helper", ("compiler_read_int", "compiler_add_helper", "compiler_multiply_helper", "compiler_div_helper")),
            ("value <- 2 * 3.\n", "compiler_multiply_helper", ("compiler_read_int", "compiler_add_helper", "compiler_subtract_helper", "compiler_div_helper")),
            ("value <- 6 / 3.\n", "compiler_div_helper", ("compiler_read_int", "compiler_add_helper", "compiler_subtract_helper", "compiler_multiply_helper")),
        ]
        for index, (source, expected_helper, absent_helpers) in enumerate(cases):
            with self.subTest(operation=index):
                result, c_path = self.transpile(source, "minimal{}".format(index))
                self.assertEqual(result.returncode, 0, result.stderr)
                generated = c_path.read_text(encoding="ascii")
                if expected_helper is None:
                    for helper in absent_helpers:
                        self.assertNotIn(helper, generated)
                else:
                    self.assertIn(expected_helper, generated)
                    for helper in absent_helpers:
                        self.assertNotIn(helper, generated)
                self.compile_generated(c_path)

    def test_semantic_errors_report_kind_and_line(self):
        cases = [
            ("sensor a:analogico(porta 1).\nsensor a:digital(porta 2).\nx <- 1.\n", "semantico", 2),
            ("sensor a:analogico(porta 1), b:digital(porta 1).\nx <- 1.\n", "semantico", 1),
            ("escrever(inexistente).\n", "semantico", 1),
            ("sensor s:analogico(porta 1).\ns <- 1.\n", "semantico", 2),
            ("atuador a:analogico(porta 1).\nx <- a.\n", "semantico", 2),
            ("sensor s:analogico(porta 1).\nler(s).\n", "semantico", 2),
            ("atuador a:digital(porta 1).\na <- 1 + 1.\n", "semantico", 2),
            ("x <- 2147483648.\n", "semantico", 1),
        ]
        for index, (source, kind, line) in enumerate(cases):
            with self.subTest(index=index):
                self.assert_compiler_error(source, kind, line, "semantic{}".format(index))

    def test_lexical_and_syntax_errors_report_lines(self):
        cases = [
            ("x <- 1 @ 2.\n", "lexico", 1),
            ("\n/* comment never closes\n", "lexico", 2),
            ("x <- 1\n", "sintatico", 2),
            ("se 1 < 2 entao fim_se.\n", "sintatico", 1),
        ]
        for index, (source, kind, line) in enumerate(cases):
            with self.subTest(index=index):
                self.assert_compiler_error(source, kind, line, "parse{}".format(index))

    def test_dynamic_digital_actuator_is_checked_at_runtime(self):
        source = "atuador a:digital(porta 1).\nler(x).\na <- x.\n"
        result, c_path = self.transpile(source, "digital-runtime")
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = self.compile_generated(c_path)

        accepted = subprocess.run(
            [str(executable)], input="1\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(accepted.returncode, 0, accepted.stderr)

        rejected = subprocess.run(
            [str(executable)], input="2\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(rejected.returncode, 1)
        self.assertIn("atuador digital a", rejected.stderr)

    def test_runtime_checks_prevent_integer_undefined_behavior(self):
        source = "ler(x).\nresult <- 10 / x.\nescrever(result).\n"
        result, c_path = self.transpile(source, "division-zero")
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = self.compile_generated(c_path)

        zero = subprocess.run(
            [str(executable)], input="0\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(zero.returncode, 1)
        self.assertIn("divisao por zero", zero.stderr)

        normal = subprocess.run(
            [str(executable)], input="2\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(normal.returncode, 0, normal.stderr)
        self.assertEqual(normal.stdout, "5\n")

        overflow_source = "minimum <- 0 - 2147483647 - 1.\none <- 0 - 1.\nvalue <- minimum / one.\n"
        overflow_result, overflow_c = self.transpile(overflow_source, "division-overflow")
        self.assertEqual(overflow_result.returncode, 0, overflow_result.stderr)
        overflow_executable = self.compile_generated(overflow_c)
        overflow = subprocess.run(
            [str(overflow_executable)], cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(overflow.returncode, 1)
        self.assertIn("fora do intervalo int", overflow.stderr)

        overflow_cases = [
            ("value <- 2147483647 + 1.\n", "overflow na soma"),
            ("minimum <- 0 - 2147483647 - 1.\nvalue <- minimum - 1.\n", "overflow na subtracao"),
            ("value <- 2147483647 * 2.\n", "overflow na multiplicacao"),
            ("minimum <- 0 - 2147483647 - 1.\nnegative_one <- 0 - 1.\nvalue <- minimum * negative_one.\n", "overflow na multiplicacao"),
        ]
        for index, (overflow_source, message) in enumerate(overflow_cases):
            with self.subTest(operation=index):
                result, generated = self.transpile(
                    overflow_source, "arithmetic-overflow{}".format(index)
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                generated_executable = self.compile_generated(generated)
                overflow_run = subprocess.run(
                    [str(generated_executable)], cwd=self.work,
                    capture_output=True, text=True, timeout=10
                )
                self.assertEqual(overflow_run.returncode, 1)
                self.assertIn(message, overflow_run.stderr)

        safe_minimum_source = "minimum <- 0 - 2147483647 - 1.\nvalue <- minimum * 1.\nescrever(value).\n"
        safe_result, safe_c = self.transpile(safe_minimum_source, "minimum-times-one")
        self.assertEqual(safe_result.returncode, 0, safe_result.stderr)
        safe_executable = self.compile_generated(safe_c)
        safe_run = subprocess.run(
            [str(safe_executable)], cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(safe_run.returncode, 0, safe_run.stderr)
        self.assertEqual(safe_run.stdout, "-2147483648\n")

    def test_input_rejects_values_outside_int_range(self):
        source = "ler(value).\nescrever(value).\n"
        result, c_path = self.transpile(source, "input-range")
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = self.compile_generated(c_path)

        minimum = subprocess.run(
            [str(executable)], input="-2147483648\n", cwd=self.work,
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(minimum.returncode, 0, minimum.stderr)
        self.assertEqual(minimum.stdout, "-2147483648\n")

        for index, value in enumerate(("2147483648", "-2147483649", "9" * 200)):
            with self.subTest(value=value[:24]):
                invalid = subprocess.run(
                    [str(executable)], input=value + "\n", cwd=self.work,
                    capture_output=True, text=True, timeout=10
                )
                self.assertEqual(invalid.returncode, 1)
                self.assertIn("inteiro valido", invalid.stderr)

    def test_debug_is_opt_in(self):
        source = "x <- 1.\n"
        quiet, _ = self.transpile(source, "quiet")
        self.assertEqual(quiet.returncode, 0, quiet.stderr)
        quiet_log = (self.work / "compile.log").read_text(encoding="ascii")
        self.assertNotIn("Lexer token:", quiet_log)

        debug, _ = self.transpile(source, "debug", debug=True)
        self.assertEqual(debug.returncode, 0, debug.stderr)
        debug_log = (self.work / "compile.log").read_text(encoding="ascii")
        self.assertIn("Lexer token:", debug_log)
        self.assertIn("Parser: dispatching command", debug_log)


if __name__ == "__main__":
    unittest.main(verbosity=2)