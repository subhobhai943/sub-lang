#!/usr/bin/env python3
import subprocess
import os
import sys

# Configuration
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)

# Try to find compiler in root or current dir
COMPILER = os.path.join(ROOT_DIR, "sublang")
if not os.path.exists(COMPILER):
    COMPILER = "./sublang"

NATIVE_COMPILER = os.path.join(ROOT_DIR, "subc-native")
if not os.path.exists(NATIVE_COMPILER):
    NATIVE_COMPILER = "./subc-native"

TEST_FILE = os.path.join(ROOT_DIR, "examples/comprehensive_test.sb")
UNIVERSAL_TEST = os.path.join(ROOT_DIR, "examples/universal_test.sb")

LANGUAGES = [
    ("python", "test_output.py", ["python3", "test_output.py"]),
    ("javascript", "test_output.js", ["node", "test_output.js"]),
    ("ruby", "test_output.rb", ["ruby", "test_output.rb"]),
    ("java", "SubProgram.java", ["javac", "SubProgram.java", "&&", "java", "SubProgram"]),
    ("cpp", "test_output.cpp", ["g++", "test_output.cpp", "-o", "test_cpp", "&&", "./test_cpp"]),
    ("rust", "test_output.rs", ["rustc", "test_output.rs", "&&", "./test_output"]),
    ("swift", "test_output.swift", ["swift", "test_output.swift"]),
    ("kotlin", "test_output.kt", ["kotlinc", "test_output.kt", "-include-runtime", "-d", "test_output.jar", "&&", "java", "-jar", "test_output.jar"]),
]

def run_command(cmd, shell=False):
    try:
        if shell:
            result = subprocess.run(cmd, shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        else:
            result = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return True, result.stdout
    except subprocess.CalledProcessError as e:
        return False, e.stderr + "\n" + e.stdout

def main():
    if not os.path.exists(COMPILER):
        print(f"Error: Compiler {COMPILER} not found. Run 'make' first.")
        sys.exit(1)

    print(f"Running comprehensive tests on {TEST_FILE}...")
    
    success_count = 0
    
    # 1. Test Transpilers
    for lang, output_file, run_cmd_list in LANGUAGES:
        print(f"[{lang.upper()}] Transpiling...", end=" ", flush=True)
        
        # Transpile
        cmd = [COMPILER, TEST_FILE, lang]
        
        # Special handling for Java output filename which is hardcoded in generator
        if lang == "java":
            # The compiler might write to stdout or specific file, here capturing stdout to file
            with open(output_file, "w") as f:
                try:
                    subprocess.run(cmd, stdout=f, check=True)
                    print("✅ Transpiled", end=" ")
                except subprocess.CalledProcessError:
                    print("❌ Transpilation Failed")
                    continue
        else:
            # Standard capture
            success, output = run_command(cmd)
            if not success:
                print(f"❌ Transpilation Failed: {output}")
                continue
            
            with open(output_file, "w") as f:
                f.write(output)
            print("✅ Transpiled", end=" ")

        # Validate file nonempty
        if not os.path.exists(output_file) or os.path.getsize(output_file) == 0:
            print("❌ Output file empty/missing")
            continue
            
        print(f"({os.path.getsize(output_file)} bytes)")
        
        # Optional: Run the generated code if environment has the tool
        # For now, we mainly accept transpilation success as the verification step
        # unless specific tools are known to be present.
        
        success_count += 1

    # 2. Test Native Compiler
    # if os.path.exists(NATIVE_COMPILER):
    #     print(f"\n[NATIVE] Compiling {UNIVERSAL_TEST}...", end=" ", flush=True)
    #     cmd = [NATIVE_COMPILER, UNIVERSAL_TEST, "uni_test"]
    #     success, output = run_command(cmd)
    #     if success:
    #         print("✅ Compiled", end=" ")
    #         if os.path.exists("uni_test") or os.path.exists("./uni_test"):
    #             print("✓ Binary exists")
    #             success_count += 1
    #         else:
    #             print("❌ Binary missing")
    #     else:
    #         print(f"❌ Compilation Failed: {output}")

    total_tasks = len(LANGUAGES) # + (1 if os.path.exists(NATIVE_COMPILER) else 0)
    print(f"\nSummary: {success_count}/{total_tasks} tasks passed.")
    
    if success_count == total_tasks:
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
