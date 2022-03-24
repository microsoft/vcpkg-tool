// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isScalar } from 'yaml';
import { Primitive } from './yaml-types';


export /** @internal */ class Coerce {
  static String(value: any): string | undefined {
    if (isScalar(value)) {
      value = value.value;
    }
    return typeof value === 'string' ? value : undefined;
  }
  static Number(value: any): number | undefined {
    if (isScalar(value)) {
      value = value.value;
    }
    return typeof value === 'number' ? value : undefined;
  }
  static Boolean(value: any): boolean | undefined {
    if (isScalar(value)) {
      value = value.value;
    }
    return typeof value === 'boolean' ? value : undefined;
  }
  static Primitive(value: any): Primitive | undefined {
    if (isScalar(value)) {
      value = value.value;
    }
    switch (typeof value) {
      case 'boolean':
      case 'number':
      case 'string':
        return value;
    }
    return undefined;
  }
}
