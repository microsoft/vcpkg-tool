// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isMap, isScalar, isSeq } from 'yaml';
import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { ValidationError } from '../interfaces/validation-error';
import { isNullish } from '../util/checks';
import { Node, Primitive, Yaml, YAMLDictionary } from './yaml-types';

/** An object that is backed by a YamlMAP node */

export /** @internal */ class Entity extends Yaml<YAMLDictionary> {
  /**@internal*/ static override create(): YAMLDictionary {
    return new YAMLDictionary();
  }

  protected setMember(name: string, value: Primitive | undefined): void {
    this.assert(true);

    if (isNullish(value)) {
      this.node.delete(name);
      return;
    }

    this.node.set(name, value);
  }

  protected getMember(name: string): Primitive | undefined {

    return this.exists() ? <Primitive | undefined>this.node?.get(name, false) : undefined;
  }

  override /** @internal */ *validate(): Iterable<ValidationError> {
    if (this.node && !isMap(this.node)) {
      yield { message: i`Incorrect type for '${this.key}' - should be an object`, range: this.sourcePosition(), category: ErrorKind.IncorrectType };
    }
  }

  has(key: string, kind?: 'sequence' | 'entity' | 'scalar'): boolean {
    if (this.node) {
      switch (kind) {
        case 'sequence':
          return isSeq(this.node.get(key));
        case 'entity':
          return isMap(this.node.get(key));
        case 'scalar':
          return isScalar(this.node.get(key));
        default:
          return this.node.has(key);
      }
    }
    return false;
  }

  kind(key: string): 'sequence' | 'entity' | 'scalar' | 'string' | 'number' | 'boolean' | 'undefined' | undefined {
    if (this.node) {
      const v = <Node>this.node.get(key, true);
      if (v === undefined) {
        return 'undefined';
      }

      if (isSeq(v)) {
        return 'sequence';
      } else if (isMap(v)) {
        return 'entity';
      } else if (isScalar(v)) {
        if (typeof v.value === 'string') {
          return 'string';
        } else if (typeof v.value === 'number') {
          return 'number';
        } else if (typeof v.value === 'boolean') {
          return 'boolean';
        }
      }
    }
    return undefined;
  }

  is(key: string, kind: 'sequence' | 'entity' | 'scalar' | 'string' | 'number' | 'boolean'): boolean | undefined {
    if (this.node) {
      const v = <Node>this.node.get(key, true);
      if (v === undefined) {
        return undefined;
      }

      switch (kind) {
        case 'sequence':
          return isSeq(v);
        case 'entity':
          return isMap(v);
        case 'scalar':
          return isScalar(v);
        case 'string':
          return isScalar(v) && typeof v.value === 'string';
        case 'number':
          return isScalar(v) && typeof v.value === 'number';
        case 'boolean':
          return isScalar(v) && typeof v.value === 'boolean';
      }
    }
    return false;
  }
}
