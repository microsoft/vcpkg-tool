// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isMap, isSeq, YAMLMap } from 'yaml';
import { Dictionary } from '../interfaces/collections';
import { ErrorKind } from '../interfaces/error-kind';
import { RegistryDeclaration } from '../interfaces/metadata/metadata-format';
import { Registry as IRegistry } from '../interfaces/metadata/registries/artifact-registry';
import { ValidationError } from '../interfaces/validation-error';
import { isFilePath, Uri } from '../util/uri';
import { Entity } from '../yaml/Entity';
import { Strings } from '../yaml/strings';
import { Node, Yaml, YAMLDictionary, YAMLSequence } from '../yaml/yaml-types';

export class Registries extends Yaml<YAMLDictionary | YAMLSequence> implements Dictionary<RegistryDeclaration>, Iterable<[string, RegistryDeclaration]>  {
  *[Symbol.iterator](): Iterator<[string, RegistryDeclaration]> {
    if (isMap(this.node)) {
      for (const { key, value } of this.node.items) {
        const v = this.createRegistry(value);
        if (v) {
          yield [key, v];
        }
      }
    }
    if (isSeq(this.node)) {
      for (const item of this.node.items) {
        if (isMap(item)) {
          const name = this.asString(item.get('name'));
          if (name) {
            const v = this.createRegistry(item);
            if (v) {
              yield [name, v];
            }
          }
        }
      }
    }
  }

  clear(): void {
    this.dispose(true);
  }

  override createNode() {
    return new YAMLSequence();
  }

  add(name: string, location?: Uri, kind?: string): RegistryDeclaration {
    if (this.get(name)) {
      throw new Error(`Registry ${name} already exists.`);
    }

    this.assert(true);
    if (isMap(this.node)) {
      throw new Error('Not Implemented as a map right now.');
    }
    if (isSeq(this.node)) {
      const m = new YAMLMap();
      this.node.add(m);
      m.set('name', name);
      m.set('location', location?.formatted);
      m.set('kind', kind);
    }
    return this.get(name)!;
  }
  delete(key: string): boolean {
    const n = this.node;
    if (isMap(n)) {
      const result = n.delete(key);
      this.dispose();
      return result;
    }
    if (isSeq(n)) {
      let removed = false;
      const items = n.items;
      for (let i = items.length - 1; i >= 0; i--) {
        const item = items[i];
        if (isMap(item) && item.get('name') === key) {
          removed ||= n.delete(i);
        }
      }
      this.dispose();
      return removed;
    }
    return false;
  }
  get(key: string): RegistryDeclaration | undefined {
    const n = this.node;
    if (isMap(n)) {
      return this.createRegistry(<Node>n.get(key, true));
    }
    if (isSeq(n)) {
      for (const item of n.items) {
        if (isMap(item) && item.get('name') === key) {
          return this.createRegistry(<Node>item);
        }
      }
    }
    return undefined;
  }

  has(key: string): boolean {
    const n = this.node;
    if (isMap(n)) {
      return n.has(key);
    }
    if (isSeq(n)) {
      for (const item of n.items) {
        if (isMap(item) && item.get('name') === key) {
          return true;
        }
      }
    }
    return false;
  }

  get length(): number {
    if (isMap(this.node) || isSeq(this.node)) {
      return this.node.items.length;
    }
    return 0;
  }
  get keys(): Array<string> {
    if (isMap(this.node)) {
      return this.node.items.map(({ key }) => this.asString(key) || '');
    }
    if (isSeq(this.node)) {
      const result = new Array<string>();
      for (const item of this.node.items) {
        if (isMap(item)) {
          const n = this.asString(item.get('name'));
          if (n) {
            result.push(n);
          }
        }
      }
      return result;
    }
    return [];
  }

  protected createRegistry(node: Node) {
    if (isMap(node)) {
      const k = this.asString(node.get('kind'));
      const l = this.asString(node.get('location'));

      // simplistic check to see if we're pointing to a file or a https:// url
      if (k === 'artifact' && l) {
        const ll = l?.toLowerCase();
        if (ll.startsWith('https://')) {
          return new RemoteRegistry(node, this);
        }
        if (isFilePath(l)) {
          return new LocalRegistry(node, this);
        }
      }

    }
    return undefined;
  }
  /** @internal */
  override *validate(): Iterable<ValidationError> {
    if (this.exists()) {
      for (const [key, registry] of this) {
        yield* registry.validate();
      }
    }
  }
}

export class Registry extends Entity implements IRegistry {

  get registryKind(): string | undefined { return this.asString(this.getMember('kind')); }
  set registryKind(value: string | undefined) { this.setMember('kind', value); }

  /** @internal */
  override *validate(): Iterable<ValidationError> {
    //
    if (this.registryKind === undefined) {
      yield {
        message: 'Registry missing \'kind\'',
        range: this,
        category: ErrorKind.FieldMissing,
      };
    }
  }
}

class LocalRegistry extends Registry {
  readonly location = new Strings(undefined, this, 'location');
  /** @internal */
  override *validate(): Iterable<ValidationError> {
    //
    if (this.registryKind !== 'artifact') {
      yield {
        message: 'Registry \'kind\' is not correct for LocalRegistry ',
        range: this,
        category: ErrorKind.IncorrectType,
      };
    }
  }
}

class RemoteRegistry extends Registry {
  readonly location = new Strings(undefined, this, 'location');
  override *validate(): Iterable<ValidationError> {
    //
    if (this.registryKind !== 'artifact') {
      yield {
        message: 'Registry \'kind\' is not correct for LocalRegistry ',
        range: this,
        category: ErrorKind.IncorrectType,
      };
    }
  }
}
