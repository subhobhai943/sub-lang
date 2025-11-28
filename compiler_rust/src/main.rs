//! SUB Language Compiler - Rust Implementation
//! High-performance, memory-safe compiler for the SUB language

use clap::{Parser, ValueEnum};
use std::path::PathBuf;
use std::time::Instant;
use anyhow::Result;

mod lexer;
mod parser;
mod semantic;
mod codegen;
mod optimizer;
mod error;
mod types;

#[derive(Debug, Clone, Copy, ValueEnum)]
enum TargetPlatform {
    Android,
    Ios,
    Windows,
    Macos,
    Linux,
    Web,
    Wasm,
}

#[derive(Parser, Debug)]
#[command(name = "subc")]
#[command(about = "SUB Language Compiler - Rust Edition", long_about = None)]
#[command(version = "2.0.0")]
struct Args {
    /// Input SUB source file (.sb)
    #[arg(value_name = "FILE")]
    input: PathBuf,

    /// Target platform
    #[arg(value_name = "PLATFORM")]
    target: TargetPlatform,

    /// Output file path
    #[arg(short, long, value_name = "FILE")]
    output: Option<PathBuf>,

    /// Optimization level (0-3)
    #[arg(short = 'O', long, default_value = "2", value_name = "LEVEL")]
    optimization: u8,

    /// Enable debug symbols
    #[arg(short, long)]
    debug: bool,

    /// Enable verbose output
    #[arg(short, long)]
    verbose: bool,

    /// Use C++ compiler backend
    #[arg(long)]
    use_cpp: bool,

    /// Enable SIMD optimizations
    #[arg(long)]
    simd: bool,

    /// Emit LLVM IR
    #[arg(long)]
    emit_llvm: bool,

    /// Parallel compilation
    #[arg(short, long)]
    parallel: bool,
}

fn main() -> Result<()> {
    // Initialize logger
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();

    let args = Args::parse();
    let start_time = Instant::now();

    if args.verbose {
        println!("SUB Language Compiler v2.0.0 (Rust Edition)");
        println!("Input: {:?}", args.input);
        println!("Target: {:?}", args.target);
        println!("Optimization: O{}", args.optimization);
    }

    // Read source file
    let source = std::fs::read_to_string(&args.input)
        .map_err(|e| anyhow::anyhow!("Failed to read input file: {}", e))?;

    // Compilation pipeline
    if args.verbose {
        println!("\n=== Lexical Analysis ===");
    }
    let tokens = lexer::tokenize(&source)?;
    
    if args.verbose {
        println!("Tokens: {} generated", tokens.len());
        println!("\n=== Parsing ===");
    }
    let ast = parser::parse(tokens)?;

    if args.verbose {
        println!("AST: Built successfully");
        println!("\n=== Semantic Analysis ===");
    }
    semantic::analyze(&ast)?;

    if args.verbose {
        println!("Semantic check: Passed");
        println!("\n=== Optimization ===");
    }
    let optimized_ast = if args.optimization > 0 {
        optimizer::optimize(ast, args.optimization)?.
    } else {
        ast
    };

    if args.verbose {
        println!("Optimization: O{} applied", args.optimization);
        println!("\n=== Code Generation ===");
    }

    let output_code = codegen::generate(
        &optimized_ast,
        args.target,
        args.use_cpp,
        args.simd,
        args.emit_llvm,
    )?;

    // Write output
    let output_path = args.output.unwrap_or_else(|| {
        let platform_suffix = match args.target {
            TargetPlatform::Android => "android",
            TargetPlatform::Ios => "ios",
            TargetPlatform::Windows => "windows",
            TargetPlatform::Macos => "macos",
            TargetPlatform::Linux => "linux",
            TargetPlatform::Web => "web",
            TargetPlatform::Wasm => "wasm",
        };
        PathBuf::from(format!("output_{}.code", platform_suffix))
    });

    std::fs::write(&output_path, output_code)
        .map_err(|e| anyhow::anyhow!("Failed to write output file: {}", e))?;

    let elapsed = start_time.elapsed();
    
    if args.verbose {
        println!("\n=== Compilation Complete ===");
        println!("Output: {:?}", output_path);
        println!("Time: {:.2}ms", elapsed.as_secs_f64() * 1000.0);
    } else {
        println!("✓ Compiled successfully in {:.2}ms", elapsed.as_secs_f64() * 1000.0);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_compilation() {
        let source = r#"
            #var x = 10
            #var y = 20
            #var sum = x + y
            #print(sum)
        "#;

        let tokens = lexer::tokenize(source).unwrap();
        assert!(!tokens.is_empty());

        let ast = parser::parse(tokens).unwrap();
        assert!(semantic::analyze(&ast).is_ok());
    }
}
