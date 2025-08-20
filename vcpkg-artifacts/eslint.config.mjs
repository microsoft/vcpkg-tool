import { FlatCompat } from "@eslint/eslintrc";
import js from "@eslint/js";
import typescriptEslint from "@typescript-eslint/eslint-plugin";
import tsParser from "@typescript-eslint/parser";
import notice from "eslint-plugin-notice";
import { defineConfig, globalIgnores } from "eslint/config";
import globals from "globals";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const compat = new FlatCompat({
    baseDirectory: __dirname,
    recommendedConfig: js.configs.recommended,
    allConfig: js.configs.all
});

export default defineConfig([globalIgnores(["**/*.d.ts", "test/scenarios/**/*", "dist/**/*", 'eslint.config.mjs']), {
    extends: compat.extends("eslint:recommended", "plugin:@typescript-eslint/recommended"),

    plugins: {
        "@typescript-eslint": typescriptEslint,
        notice,
    },

    languageOptions: {
        globals: {
            ...globals.node,
            Atomics: "readonly",
            SharedArrayBuffer: "readonly",
        },

        parser: tsParser,
        ecmaVersion: 2022,
        sourceType: "module",
        parserOptions: {
            projectService: true,
            tsconfigRootDir: import.meta.dirname
        }
    },

    rules: {
        "no-trailing-spaces": "error",
        "space-in-parens": "error",

        "keyword-spacing": ["error", {
            overrides: {
                this: {
                    before: false,
                },
            },
        }],

        "@typescript-eslint/no-floating-promises": "error",

        "@typescript-eslint/consistent-type-assertions": ["error", {
            assertionStyle: "angle-bracket",
        }],

        "@typescript-eslint/no-explicit-any": "off",
        "@typescript-eslint/no-unused-vars": ["error", {
            argsIgnorePattern: "^_"
        }],

        "@typescript-eslint/array-type": ["error", {
            default: "generic",
        }],

        indent: ["warn", 2, {
            SwitchCase: 1,
            ObjectExpression: "first",
        }],

        "@typescript-eslint/indent": [0, 2],
        "linebreak-style": ["error", "unix"],
        quotes: ["error", "single"],
        semi: ["error", "always"],

        "no-multiple-empty-lines": ["error", {
            max: 2,
            maxBOF: 0,
            maxEOF: 1,
        }],

        "notice/notice": ["error", {
            templateFile: "./header.txt",
        }],

        "no-eval": "error",
        "no-implied-eval": "off",
        "@typescript-eslint/no-implied-eval": "error",

        "no-restricted-syntax": ["error", {
            selector: "CallExpression[callee.name='execUnsafeLocalFunction']",
            message: "execUnsafeLocalFunction is banned",
        }, {
            selector: "CallExpression[callee.property.name='execUnsafeLocalFunction']",
            message: "execUnsafeLocalFunction is banned",
        }, {
            selector: "CallExpression[callee.name='setInnerHTMLUnsafe']",
            message: "setInnerHTMLUnsafe is banned",
        }, {
            selector: "CallExpression[callee.property.name='setInnerHTMLUnsafe']",
            message: "setInnerHTMLUnsafe is banned",
        }],
    },
}]);
