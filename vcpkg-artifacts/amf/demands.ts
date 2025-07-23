// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isMap, isScalar } from 'yaml';
import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { ValidationMessage } from '../interfaces/validation-message';
import { parseQuery } from '../mediaquery/media-query';
import { Entity } from '../yaml/Entity';
import { EntityMap } from '../yaml/EntityMap';
import { Primitive, Yaml, YAMLDictionary } from '../yaml/yaml-types';
import { Exports } from './exports';
import { Installs } from './installer';
import { Requires } from './Requires';

const ignore = new Set<string>(['info', 'contacts', 'error', 'message', 'warning', 'requires']);
/**
 * A map of mediaquery to DemandBlock
 */
export class Demands extends EntityMap<YAMLDictionary, DemandBlock> {
  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(DemandBlock, node, parent, key);
  }

  override get keys() {
    return super.keys.filter(each => !ignore.has(each));
  }

  /** @internal */
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();

    for (const [mediaQuery, demandBlock] of this) {
      if (ignore.has(mediaQuery)) {
        continue;
      }
      if (!isMap(demandBlock.node)) {
        yield {
          message: `Conditional demand '${mediaQuery}' is not an object`,
          range: (<any>demandBlock.node).range || [0, 0, 0],
          category: ErrorKind.IncorrectType
        };
        continue;
      }

      const query = parseQuery(mediaQuery);
      if (!query.isValid) {
        yield { message: i`Error parsing conditional demand '${mediaQuery}'- ${query.error?.message}`, range: this.sourcePosition(mediaQuery)/* mediaQuery.range! */, rangeOffset: query.error, category: ErrorKind.ParseError };
        continue;
      }

      yield* demandBlock.validate();
    }
  }
}

export class DemandBlock extends Entity {
  discoveredData = <Record<string, string>>{};

  get error(): string | undefined { return this.asString(this.getMember('error')); }
  set error(value: string | undefined) { this.setMember('error', value); }

  get warning(): string | undefined { return this.asString(this.getMember('warning')); }
  set warning(value: string | undefined) { this.setMember('warning', value); }

  get message(): string | undefined { return this.asString(this.getMember('message')); }
  set message(value: string | undefined) { this.setMember('message', value); }

  readonly requires = new Requires(undefined, this, 'requires');
  readonly exports = new Exports(undefined, this, 'exports');
  readonly install = new Installs(undefined, this, 'install');

  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(node, parent, key);
  }

  /** @internal */
  override *validate(): Iterable<ValidationMessage> {
    yield* this.validateChildKeys(['error', 'warning', 'message', 'requires', 'exports', 'install']);

    yield* super.validate();
    if (this.exists()) {
      yield* this.validateChild('error', 'string');
      yield* this.validateChild('warning', 'string');
      yield* this.validateChild('message', 'string');

      yield* this.exports.validate();
      yield* this.requires.validate();
      yield* this.install.validate();
    }
  }

  private evaluate(value: string) {
    if (!value || value.indexOf('$') === -1) {
      // quick exit if no expression or no variables
      return value;
    }

    // $$ -> escape for $
    value = value.replace(/\$\$/g, '\uffff');

    // $0 ... $9 -> replace contents with the values from the artifact
    value = value.replace(/\$([0-9])/g, (match) => this.discoveredData[match] || match);

    // restore escaped $
    return value.replace(/\uffff/g, '$');
  }

  override asString(value: any): string | undefined {
    if (value === undefined) {
      return value;
    }
    return this.evaluate(isScalar(value) ? value.value : value);
  }

  override asPrimitive(value: any): Primitive | undefined {
    if (value === undefined) {
      return value;
    }
    if (isScalar(value)) {
      value = value.value;
    }
    switch (typeof value) {
      case 'boolean':
      case 'number':
        return value;

      case 'string': {
        return this.evaluate(value);
      }
    }
    return undefined;
  }
}
