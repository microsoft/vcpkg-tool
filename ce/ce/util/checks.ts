// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isScalar, isSeq, YAMLMap } from 'yaml';
import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { ValidationError } from '../interfaces/validation-error';

/** @internal */
export function isPrimitive(value: any): value is (string | number | boolean) {
  switch (typeof value) {
    case 'string':
    case 'number':
    case 'boolean':
      return true;
  }
  return false;
}

/** @internal */
export function isNullish(value: any): value is null | undefined {
  return value === null || value === undefined || value === '';
}

/** @internal */
export function isIterable<T>(source: any): source is Iterable<T> {
  return !!source && typeof (source) !== 'string' && !!source[Symbol.iterator];
}

export function* checkOptionalString(parent: YAMLMap, range: [number, number, number], name: string): Iterable<ValidationError> {
  switch (typeof parent.get(name)) {
    case 'string':
    case 'undefined':
      break;
    default:
      yield { message: i`${name} must be a string`, range: range, category: ErrorKind.IncorrectType };
  }
}

export function* checkOptionalBool(parent: YAMLMap, range: [number, number, number], name: string): Iterable<ValidationError> {
  switch (typeof parent.get(name)) {
    case 'boolean':
    case 'undefined':
      break;
    default:
      yield { message: i`${name} must be a bool`, range: range, category: ErrorKind.IncorrectType };
  }
}

function checkOptionalArrayOfStringsImpl(parent: YAMLMap, range: [number, number, number], name: string): boolean {
  const val = parent.get(name);
  if (isSeq(val)) {
    for (const entry of val.items) {
      if (!isScalar(entry) || typeof entry.value !== 'string') {
        return true;
      }
    }
  } else if (typeof val !== 'undefined') {
    return true;
  }

  return false;
}

export function* checkOptionalArrayOfStrings(parent: YAMLMap, range: [number, number, number], name: string): Iterable<ValidationError> {
  if (checkOptionalArrayOfStringsImpl(parent, range, name)) {
    yield { message: i`${name} must be an array of strings, or unset`, range: range, category: ErrorKind.IncorrectType };
  }
}
