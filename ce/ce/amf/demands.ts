// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { stream } from 'fast-glob';
import { lstat, Stats } from 'fs';
import { delimiter, join, resolve } from 'path';
import { isMap, isScalar } from 'yaml';
import { Activation } from '../artifacts/activation';
import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { AlternativeFulfillment } from '../interfaces/metadata/alternative-fulfillment';
import { ValidationError } from '../interfaces/validation-error';
import { parseQuery } from '../mediaquery/media-query';
import { Session } from '../session';
import { Evaluator } from '../util/evaluator';
import { cmdlineToArray, execute } from '../util/exec-cmd';
import { createSandbox } from '../util/safeEval';
import { Entity } from '../yaml/Entity';
import { EntityMap } from '../yaml/EntityMap';
import { Strings } from '../yaml/strings';
import { Primitive, Yaml, YAMLDictionary } from '../yaml/yaml-types';
import { Installs } from './installer';
import { Requires } from './Requires';
import { Settings } from './settings';

/** sandboxed eval function for evaluating expressions */
const safeEval: <T>(code: string, context?: any) => T = createSandbox();
const hostFeatures = new Set<string>(['x64', 'x86', 'arm', 'arm64', 'windows', 'linux', 'osx', 'freebsd']);

const ignore = new Set<string>(['info', 'contacts', 'error', 'message', 'warning', 'requires', 'see-also']);
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
  override *validate(): Iterable<ValidationError> {
    yield* super.validate();

    for (const [mediaQuery, demandBlock] of this) {
      if (ignore.has(mediaQuery)) {
        continue;
      }
      if (!isMap(demandBlock.node)) {
        yield {
          message: `Conditional demand '${mediaQuery}' is not an object`,
          range: demandBlock.node!.range || [0, 0, 0],
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
  #environment: Record<string, string | number | boolean | undefined> = {};
  #activation?: Activation;
  #data?: Record<string, string>;

  setActivation(activation?: Activation) {
    this.#activation = activation;
  }

  setData(data: Record<string, string>) {
    this.#data = data;
  }

  setEnvironment(env: Record<string, string | number | boolean | undefined>) {
    this.#environment = env;
  }

  protected get evaluationBlock() {
    return new Evaluator(this.#data || {}, this.#environment, this.#activation?.output || {});
  }
  get error(): string | undefined { return this.usingAlternative ? this.unless.error : this.asString(this.getMember('error')); }
  set error(value: string | undefined) { this.setMember('error', value); }

  get warning(): string | undefined { return this.usingAlternative ? this.unless.warning : this.asString(this.getMember('warning')); }
  set warning(value: string | undefined) { this.setMember('warning', value); }

  get message(): string | undefined { return this.usingAlternative ? this.unless.warning : this.asString(this.getMember('message')); }
  set message(value: string | undefined) { this.setMember('message', value); }

  get seeAlso(): Requires {
    return this.usingAlternative ? this.unless.seeAlso : this._seeAlso;
  }

  get requires(): Requires {
    return this.usingAlternative ? this.unless.requires : this._requires;
  }

  get settings(): Settings {
    return this.usingAlternative ? this.unless.settings : this._settings;
  }

  get install(): Installs {
    return this.usingAlternative ? this.unless.install : this._install;
  }

  protected readonly _seeAlso = new Requires(undefined, this, 'seeAlso');
  protected readonly _requires = new Requires(undefined, this, 'requires');
  protected readonly _settings = new Settings(undefined, this, 'settings');
  protected readonly _install = new Installs(undefined, this, 'install');

  readonly unless!: Unless;

  protected usingAlternative: boolean | undefined;

  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(node, parent, key);
    if (key !== 'unless') {
      this.unless = new Unless(undefined, this, 'unless');
    }
  }

  /**
   * Async Initializer.
   *
   * checks the alternative demand resolution.
   * when this runs, if the alternative is met, the rest of the demand is redirected to the alternative.
   */
  async init(session: Session): Promise<DemandBlock> {
    this.#environment = session.environment;
    if (this.usingAlternative === undefined && this.has('unless')) {
      await this.unless.init(session);
      this.usingAlternative = this.unless.usingAlternative;
    }
    return this;
  }

  /** @internal */
  override *validate(): Iterable<ValidationError> {
    yield* super.validate();
    if (this.exists()) {
      yield* this.settings.validate();
      yield* this.requires.validate();
      yield* this.seeAlso.validate();
      yield* this.install.validate();
    }
  }

  override asString(value: any): string | undefined {
    if (value === undefined) {
      return value;
    }
    return this.evaluationBlock.evaluate(isScalar(value) ? value.value : value);
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
        return this.evaluationBlock.evaluate(value);
      }
    }
    return undefined;
  }
}

/** Expands string variables in a string */
function expandStrings(sandboxData: Record<string, any>, value: string) {
  let n = undefined;

  // allow $PATH instead of ${PATH} -- simplifies YAML strings
  value = value.replace(/\$([a-zA-Z0-9.]+)/g, '${$1}');

  const parts = value.split(/(\${\S+?})/g).filter(each => each).map((each, i) => {
    const v = each.replace(/^\${(.*)}$/, (m, match) => safeEval(match, sandboxData) ?? each);

    if (v.indexOf(delimiter) !== -1) {
      n = i;
    }

    return v;
  });

  if (n === undefined) {
    return parts.join('');
  }

  const front = parts.slice(0, n).join('');
  const back = parts.slice(n + 1).join('');

  return parts[n].split(delimiter).filter(each => each).map(each => `${front}${each}${back}`).join(delimiter);
}

/** filters output and produces a sandbox context object */
function filter(expression: string, content: string) {
  const parsed = /^\/(.*)\/(\w*)$/.exec(expression);
  const output = <any>{
    $content: content
  };
  if (parsed) {
    const filtered = new RegExp(parsed[1], parsed[2]).exec(content);

    if (filtered) {
      for (const [i, v] of filtered.entries()) {
        if (i === 0) {
          continue;
        }
        output[`$${i}`] = v;
      }
    }
  }
  return output;
}

export class Unless extends DemandBlock implements AlternativeFulfillment {

  readonly from = new Strings(undefined, this, 'from');
  readonly where = new Strings(undefined, this, 'where');

  get run(): string | undefined { return this.asString(this.getMember('run')); }
  set run(value: string | undefined) { this.setMember('run', value); }

  get select(): string | undefined { return this.asString(this.getMember('select')); }
  set select(value: string | undefined) { this.setMember('select', value); }

  get matches(): string | undefined { return this.asString(this.getMember('is')); }
  set matches(value: string | undefined) { this.setMember('is', value); }

  /** @internal */
  override *validate(): Iterable<ValidationError> {
    // todo: what other validations do we need?
    yield* super.validate();
    if (this.has('unless')) {
      yield {
        message: '"unless" is not supported in an unless block',
        range: this.sourcePosition('unless'),
        category: ErrorKind.InvalidDefinition
      };
    }
  }

  override async init(session: Session): Promise<Unless> {
    this.setEnvironment(session.environment);
    if (this.usingAlternative === undefined) {
      this.usingAlternative = false;
      if (this.from.length > 0 && this.where.length > 0) {
        // we're doing some kind of check.
        const locations = [...this.from].map(each => expandStrings(this.evaluationBlock, each).split(delimiter)).flat();
        const binaries = [...this.where].map(each => expandStrings(this.evaluationBlock, each));

        const search = locations.map(location => binaries.map(binary => join(location, binary).replace(/\\/g, '/'))).flat();

        // when we find an adequate match, we stop looking
        // to do so and not work hrd

        const Break = <NodeJS.ErrnoException>{};
        for await (const item of stream(search, {
          concurrency: 1,
          stats: false, fs: <any>{
            lstat: (path: string, callback: (error: NodeJS.ErrnoException | null, stats: Stats) => void) => {
              // if we're done iterating, always return an error.
              if (this.usingAlternative) {
                return callback(Break, <Stats><any>undefined);
              }

              return lstat(path, (error, stats) => {
                // just return an error, as we don't want more results.
                if (this.usingAlternative) {
                  // just return an error, as we don't want more results.
                  return callback(Break, <Stats><any>undefined);
                }

                // symlink'd binaries on windows give us errors when it interrogates it too much.
                if (stats && stats.mode === 41398) {
                  stats.mode = stats.mode & ~8192;
                }
                return callback(error, stats);
              });
            }
          }
        })) {
          // we found something that looks promising.
          let filtered = <any>{ $0: item };
          this.setData(filtered);
          if (this.run) {

            const commandline = cmdlineToArray(this.run.replace('$0', item.toString()));
            const result = await execute(resolve(commandline[0]), commandline.slice(1));
            if (result.code !== 0) {
              continue;
            }

            filtered = filter(this.select || '', result.log);
            filtered.$0 = item;

            // if we have a match expression, let's check it.
            if (this.matches && !safeEval(this.matches, filtered)) {
              continue; // not a match, move on
            }

            // it did match, or it's just presence check
            this.usingAlternative = true;
            // set the data output of the check
            // this is used later to fill in the settings.
            this.setData(filtered);
            return this;
          }
        }
      }
    }
    return this;
  }

  override get error(): string | undefined { return this.asString(this.getMember('error')); }
  override get warning(): string | undefined { return this.asString(this.getMember('warning')); }
  override get message(): string | undefined { return this.asString(this.getMember('message')); }

  override get seeAlso(): Requires {
    return this._seeAlso;
  }

  override get requires(): Requires {
    return this._requires;
  }

  override get settings(): Settings {
    return this._settings;
  }

  override get install(): Installs {
    return this._install;
  }

}
