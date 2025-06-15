# Build Configuration Files

This directory contains configuration files used by the CI/CD pipeline for build validation and testing.

-   Defines shader compilation parameters, known warnings, and expected build configurations
-   Used by the GitHub Actions workflow to validate shader compilation during CI builds
-   Not intended for direct user interaction - this is build infrastructure configuration

## Files

-   `shader-validation.yaml`: Configuration for shader compilation validation using hlslkit
-   `shader-validation-vr.yaml`: VR Configuration for shader compilation validation using hlslkit

## Usage

These files are automatically used by the GitHub Actions workflows and should not be modified unless you're [updating](https://github.com/alandtse/hlslkit/blob/main/README.md#workflow) the build validation process.
