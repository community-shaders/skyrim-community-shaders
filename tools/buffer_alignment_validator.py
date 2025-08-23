#!/usr/bin/env python3
"""
GPU Buffer Alignment Validator

This tool validates that HLSL cbuffer structures are properly aligned to 16-byte boundaries.
It parses HLSL files and reports any alignment issues with clear error messages.

Usage:
    python buffer_alignment_validator.py [options] <hlsl_files_or_directories>

Options:
    --strict        Fail on any alignment warnings (default: False)
    --fix           Automatically add alignment directives (default: False)
    --quiet         Only show errors, not warnings (default: False)
    --output-dir    Directory to write fixed files (default: same as input)
"""

import re
import os
import sys
import argparse
from pathlib import Path
from typing import List, Dict, Tuple, Optional, NamedTuple
from dataclasses import dataclass

# HLSL type sizes in bytes
HLSL_TYPE_SIZES = {
    'float': 4, 'float1': 4, 'float2': 8, 'float3': 12, 'float4': 16,
    'int': 4, 'int1': 4, 'int2': 8, 'int3': 12, 'int4': 16,
    'uint': 4, 'uint1': 4, 'uint2': 8, 'uint3': 12, 'uint4': 16,
    'bool': 4, 'bool1': 4, 'bool2': 8, 'bool3': 12, 'bool4': 16,
    'half': 2, 'half1': 2, 'half2': 4, 'half3': 6, 'half4': 8,
    'double': 8, 'double1': 8, 'double2': 16, 'double3': 24, 'double4': 32,
    # Matrix types (column-major by default)
    'float2x2': 16, 'float3x3': 48, 'float4x4': 64,
    'float2x3': 24, 'float2x4': 32, 'float3x2': 24, 'float3x4': 48,
    'float4x2': 32, 'float4x3': 48,
    # Row-major matrices (when specified)
    'row_major float2x2': 16, 'row_major float3x3': 48, 'row_major float4x4': 64,
    'row_major float2x3': 32, 'row_major float2x4': 32, 'row_major float3x2': 24,
    'row_major float3x4': 48, 'row_major float4x2': 32, 'row_major float4x3': 48,
}

# Buffer alignment requirement (16 bytes for GPU constant buffers)
BUFFER_ALIGNMENT = 16

@dataclass
class BufferMember:
    """Represents a member of a cbuffer structure."""
    name: str
    type_name: str
    array_size: Optional[int] = None
    packoffset: Optional[str] = None
    line_number: int = 0
    
    def get_size_bytes(self) -> int:
        """Calculate the size of this member in bytes."""
        base_size = HLSL_TYPE_SIZES.get(self.type_name.strip(), 0)
        if base_size == 0:
            # Try to handle complex types or unknown types
            if 'float4x4' in self.type_name:
                base_size = 64
            elif 'float3x3' in self.type_name:
                base_size = 48
            elif 'float4' in self.type_name:
                base_size = 16
            elif 'float3' in self.type_name:
                base_size = 12
            elif 'float2' in self.type_name:
                base_size = 8
            elif 'float' in self.type_name:
                base_size = 4
            elif 'uint' in self.type_name or 'int' in self.type_name:
                base_size = 4
            else:
                return 0  # Unknown type
        
        # Handle arrays
        if self.array_size:
            # For basic arrays within cbuffers, just multiply by count
            # Only certain buffer types need per-element alignment
            if 'float4x4' in self.type_name or base_size >= 16:
                # Large types may need alignment
                aligned_element_size = ((base_size + 15) // 16) * 16
                return aligned_element_size * self.array_size
            else:
                # Simple arrays just multiply by count
                return base_size * self.array_size
        
        return base_size

@dataclass
class BufferInfo:
    """Information about a cbuffer structure."""
    name: str
    register: str
    members: List[BufferMember]
    line_start: int
    line_end: int
    
    def get_total_size(self) -> int:
        """Calculate total size of the buffer."""
        return sum(member.get_size_bytes() for member in self.members)
    
    def is_aligned(self) -> bool:
        """Check if the buffer is properly aligned."""
        return self.get_total_size() % BUFFER_ALIGNMENT == 0
    
    def get_padding_needed(self) -> int:
        """Get the number of bytes needed for alignment."""
        total_size = self.get_total_size()
        return (BUFFER_ALIGNMENT - (total_size % BUFFER_ALIGNMENT)) % BUFFER_ALIGNMENT

class BufferAlignmentValidator:
    """Validates HLSL buffer alignment."""
    
    def __init__(self, strict: bool = False, fix: bool = False, quiet: bool = False):
        self.strict = strict
        self.fix = fix
        self.quiet = quiet
        self.errors = []
        self.warnings = []
    
    def parse_hlsl_file(self, file_path: Path) -> List[BufferInfo]:
        """Parse an HLSL file and extract buffer information."""
        buffers = []
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            self.errors.append(f"Error reading {file_path}: {e}")
            return buffers
        
        # Find cbuffer declarations
        cbuffer_pattern = r'cbuffer\s+(\w+)\s*:\s*register\s*\(\s*([^)]+)\s*\)\s*\{([^}]+)\}'
        
        for match in re.finditer(cbuffer_pattern, content, re.MULTILINE | re.DOTALL):
            buffer_name = match.group(1)
            register = match.group(2)
            buffer_content = match.group(3)
            
            # Calculate line numbers
            lines_before = content[:match.start()].count('\n')
            lines_in_buffer = buffer_content.count('\n')
            
            members = self._parse_buffer_members(buffer_content, lines_before)
            
            buffer_info = BufferInfo(
                name=buffer_name,
                register=register,
                members=members,
                line_start=lines_before + 1,
                line_end=lines_before + lines_in_buffer + 1
            )
            
            buffers.append(buffer_info)
        
        return buffers
    
    def _parse_buffer_members(self, buffer_content: str, line_offset: int) -> List[BufferMember]:
        """Parse members from buffer content."""
        members = []
        lines = buffer_content.strip().split('\n')
        
        for i, line in enumerate(lines):
            line = line.strip()
            if not line or line.startswith('//') or line.startswith('#'):
                continue
            
            # Remove comments
            if '//' in line:
                line = line[:line.index('//')]
            
            # Parse member declaration
            member = self._parse_member_line(line, line_offset + i + 1)
            if member:
                members.append(member)
        
        return members
    
    def _parse_member_line(self, line: str, line_number: int) -> Optional[BufferMember]:
        """Parse a single member declaration line."""
        line = line.strip().rstrip(';')
        
        if not line:
            return None
        
        # Handle packoffset
        packoffset = None
        if ':' in line and 'packoffset' in line:
            parts = line.split(':')
            line = parts[0].strip()
            packoffset_part = parts[1].strip()
            packoffset_match = re.search(r'packoffset\s*\(\s*([^)]+)\s*\)', packoffset_part)
            if packoffset_match:
                packoffset = packoffset_match.group(1)
        
        # Parse type and name (with optional array)
        # Handle row_major prefix
        type_pattern = r'(?:(row_major)\s+)?(\w+(?:\d+x\d+)?)\s+(\w+)(?:\[(\d+)\])?'
        match = re.match(type_pattern, line)
        
        if not match:
            return None
        
        row_major = match.group(1)
        type_name = match.group(2)
        var_name = match.group(3)
        array_size = int(match.group(4)) if match.group(4) else None
        
        # Combine row_major with type name if present
        if row_major:
            type_name = f"{row_major} {type_name}"
        
        return BufferMember(
            name=var_name,
            type_name=type_name,
            array_size=array_size,
            packoffset=packoffset,
            line_number=line_number
        )
    
    def validate_file(self, file_path: Path) -> bool:
        """Validate a single HLSL file."""
        if not self.quiet:
            print(f"Validating {file_path}")
        
        buffers = self.parse_hlsl_file(file_path)
        all_valid = True
        
        for buffer in buffers:
            valid = self._validate_buffer(buffer, file_path)
            all_valid = all_valid and valid
        
        return all_valid
    
    def _validate_buffer(self, buffer: BufferInfo, file_path: Path) -> bool:
        """Validate a single buffer."""
        if buffer.is_aligned():
            if not self.quiet:
                print(f"  ✓ cbuffer {buffer.name} is properly aligned ({buffer.get_total_size()} bytes)")
            return True
        
        padding_needed = buffer.get_padding_needed()
        message = (f"  ✗ cbuffer {buffer.name} is NOT aligned to 16-byte boundary\n"
                  f"    File: {file_path}:{buffer.line_start}-{buffer.line_end}\n"
                  f"    Current size: {buffer.get_total_size()} bytes\n"
                  f"    Padding needed: {padding_needed} bytes\n"
                  f"    Suggested fix: Add 'uint pad0[{(padding_needed + 3) // 4}];' as last member")
        
        if self.strict:
            self.errors.append(message)
        else:
            self.warnings.append(message)
        
        print(message)
        return False
    
    def validate_directory(self, directory: Path) -> bool:
        """Validate all HLSL files in a directory."""
        all_valid = True
        
        hlsl_files = list(directory.rglob("*.hlsl")) + list(directory.rglob("*.hlsli"))
        
        for file_path in hlsl_files:
            valid = self.validate_file(file_path)
            all_valid = all_valid and valid
        
        return all_valid
    
    def print_summary(self):
        """Print validation summary."""
        print("\n" + "="*60)
        print("BUFFER ALIGNMENT VALIDATION SUMMARY")
        print("="*60)
        
        if self.errors:
            print(f"ERRORS: {len(self.errors)}")
            for error in self.errors:
                print(error)
        
        if self.warnings:
            print(f"WARNINGS: {len(self.warnings)}")
            for warning in self.warnings:
                print(warning)
        
        if not self.errors and not self.warnings:
            print("✓ All buffers are properly aligned!")
        
        print("="*60)

def main():
    parser = argparse.ArgumentParser(description="Validate GPU buffer alignment in HLSL files")
    parser.add_argument("paths", nargs="+", help="HLSL files or directories to validate")
    parser.add_argument("--strict", action="store_true", help="Treat alignment warnings as errors")
    parser.add_argument("--fix", action="store_true", help="Automatically fix alignment issues")
    parser.add_argument("--quiet", action="store_true", help="Only show errors and warnings")
    
    args = parser.parse_args()
    
    validator = BufferAlignmentValidator(strict=args.strict, fix=args.fix, quiet=args.quiet)
    all_valid = True
    
    for path_str in args.paths:
        path = Path(path_str)
        
        if not path.exists():
            print(f"Error: Path {path} does not exist")
            all_valid = False
            continue
        
        if path.is_file():
            if path.suffix.lower() in ['.hlsl', '.hlsli']:
                valid = validator.validate_file(path)
                all_valid = all_valid and valid
            else:
                print(f"Skipping non-HLSL file: {path}")
        elif path.is_dir():
            valid = validator.validate_directory(path)
            all_valid = all_valid and valid
        else:
            print(f"Error: {path} is neither a file nor directory")
            all_valid = False
    
    validator.print_summary()
    
    # Exit with error code if validation failed
    if validator.errors or (args.strict and validator.warnings):
        sys.exit(1)
    
    sys.exit(0)

if __name__ == "__main__":
    main()