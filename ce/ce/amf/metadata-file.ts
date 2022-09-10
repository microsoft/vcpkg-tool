// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { Document, isMap, LineCounter, parseDocument, YAMLMap } from 'yaml';
import { Registry } from '../artifacts/registry';
import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { Profile } from '../interfaces/metadata/metadata-format';
import { ValidationMessage } from '../interfaces/validation-message';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { BaseMap } from '../yaml/BaseMap';
import { Options } from '../yaml/Options';
import { Yaml, YAMLDictionary } from '../yaml/yaml-types';
import { Contacts } from './contact';
import { DemandBlock, Demands } from './demands';
import { GlobalSettings } from './global-settings';
import { Info } from './info';
import { Registries } from './registries';

export class MetadataFile extends BaseMap implements Profile {
  session!: Session;

  private constructor(protected document: Document.Parsed, public readonly filename: string, public lineCounter: LineCounter, public readonly file: Uri, public readonly registry: Registry | undefined) {
    super(<YAMLMap<string, any>><any>document.contents);
  }

  async init(session: Session): Promise<MetadataFile> {
    await this.demandBlock.init(session);
    return this;
  }

  static async parseMetadata(uri: Uri, session: Session, registry?: Registry): Promise<MetadataFile> {
    return MetadataFile.parseConfiguration(uri.path, await uri.readUTF8(), session, registry);
  }

  static async parseConfiguration(filename: string, content: string, session: Session, registry?: Registry): Promise<MetadataFile> {
    const lc = new LineCounter();
    if (!content || content === 'null') {
      content = '{\n}';
    }
    const doc = parseDocument(content, { prettyErrors: false, lineCounter: lc, strict: true });
    return new MetadataFile(doc, filename, lc, session.parseUri(filename), registry).init(session);
  }

  #info = new Info(undefined, this, 'info');

  contacts = new Contacts(undefined, this, 'contacts');
  registries = new Registries(undefined, this, 'registries');
  globalSettings = new GlobalSettings(undefined, this, 'global');

  // rather than re-implement it, use encapsulation with a demand block
  private demandBlock = new DemandBlock(this.node, undefined);

  /** Artifact identity
 *
 * this should be the 'path' to the artifact (following the guidelines)
 *
 * ie, 'compilers/microsoft/msvc'
 *
 * artifacts install to artifacts-root/<source>/<id>/<VER>
 */
  get id(): string { return this.asString(this.getMember('id')) || this.#info.id || ''; }
  set id(value: string) { this.normalize(); this.setMember('id', value); }

  /** the version of this artifact */
  get version(): string { return this.asString(this.getMember('version')) || this.#info.version || ''; }
  set version(value: string) { this.normalize(); this.setMember('version', value); }

  /** a short 1 line descriptive text */
  get summary(): string | undefined { return this.asString(this.getMember('summary')) || this.#info.summary; }
  set summary(value: string | undefined) { this.normalize(); this.setMember('summary', value); }

  /** if a longer description is required, the value should go here */
  get description(): string | undefined { return this.asString(this.getMember('description')) || this.#info.description; }
  set description(value: string | undefined) { this.normalize(); this.setMember('description', value); }

  readonly #options = new Options(undefined, this, 'options');

  /** if true, intended to be used only as a dependency; for example, do not show in search results or lists */
  get dependencyOnly(): boolean { return this.#options.has('dependencyOnly') || this.#info.options.has('dependencyOnly'); }
  get espidf(): boolean { return this.#options.has('espidf') || this.#info.options.has('espidf'); }

  /** higher priority artifacts should install earlier; the default is zero */
  get priority(): number { return this.asNumber(this.getMember('priority')) || this.#info.priority || 0; }
  set priority(value: number) { this.normalize(); this.setMember('priority', value); }

  get error(): string | undefined { return this.demandBlock.error; }
  set error(value: string | undefined) { this.demandBlock.error = value; }

  get warning(): string | undefined { return this.demandBlock.warning; }
  set warning(value: string | undefined) { this.demandBlock.warning = value; }

  get message(): string | undefined { return this.demandBlock.message; }
  set message(value: string | undefined) { this.demandBlock.message = value; }

  get seeAlso() { return this.demandBlock.seeAlso; }
  get requires() { return this.demandBlock.requires; }
  get exports() { return this.demandBlock.exports; }
  get install() { return this.demandBlock.install; }

  conditionalDemands = new Demands(undefined, this, 'demands');

  get isFormatValid(): boolean {
    return this.document.errors.length === 0;
  }

  toJsonString() {
    let content = JSON.stringify(this.document.toJSON(), null, 2);
    if (!content || content === 'null') {
      content = '{}\n';
    }

    return content;
  }

  async save(uri: Uri = this.file): Promise<void> {
    await uri.writeUTF8(this.toJsonString());
  }

  #errors!: Array<string>;
  get formatErrors(): Array<string> {
    const t = this;
    return this.#errors || (this.#errors = this.document.errors.map(each => {
      const message = each.message;
      const line = each.linePos?.[0].line || 1;
      const column = each.linePos?.[0].col || 1;
      return t.formatMessage(each.name, message, line, column);
    }));
  }

  /** @internal */ formatMessage(category: ErrorKind | string, message: string, line?: number, column?: number): string {
    if (line !== undefined && column !== undefined) {
      return `${this.filename}:${line}:${column} ${category}, ${message}`;
    } else {
      return `${this.filename}: ${category}, ${message}`;
    }
  }

  formatVMessage(vMessage: ValidationMessage): string {
    const message = vMessage.message;
    const range = vMessage.range;
    const rangeOffset = vMessage.rangeOffset;
    const category = vMessage.category;
    const r = Array.isArray(range) ? range : range?.sourcePosition();
    const { line, column } = this.positionAt(r, rangeOffset);

    return this.formatMessage(category, message, line, column);
  }

  *deprecationWarnings(): Iterable<ValidationMessage> {
    const node = this.node;
    if (node) {
      const info = node.get('info');
      if (info) {
        const infoNode = <YAMLMap>info;
        yield {
          message: i`The info block is deprecated for consistency with vcpkg.json; move info members to the outside.`,
          range: infoNode.range || undefined,
          category: ErrorKind.InfoBlockPresent
        };
      }
    }
  }

  private positionAt(range?: [number, number, number?], offset?: { line: number, column: number }) {
    const { line, col } = this.lineCounter.linePos(range?.[0] || 0);

    return offset ? {
      // adds the offset values (which can come from the mediaquery parser) to the line & column. If MQ doesn't have a position, it's zero.
      line: line + (offset.line - 1),
      column: col + (offset.column - 1),
    } :
      {
        line, column: col
      };
  }

  /** @internal */
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    const hasInfo = this.document.has('info');
    const allowedChildren = ['contacts', 'registries', 'global', 'demands', 'exports', 'requires', 'install', 'seeAlso'];

    if (hasInfo) {
      // 2022-06-17 and earlier used a separate 'info' block for these fields
      allowedChildren.push('info');
    } else {
      allowedChildren.push('version', 'id', 'summary', 'priority', 'description', 'options');
    }

    yield* this.validateChildKeys(allowedChildren);

    if (hasInfo) {
      yield* this.#info.validate();
    } else {
      if (!this.has('id')) {
        yield { message: i`Missing identity '${'id'}'`, range: this, category: ErrorKind.FieldMissing };
      } else if (!this.childIs('id', 'string')) {
        yield { message: i`id should be of type 'string', found '${this.kind('id')}'`, range: this.sourcePosition('id'), category: ErrorKind.IncorrectType };
      }

      if (!this.has('version')) {
        yield { message: i`Missing version '${'version'}'`, range: this, category: ErrorKind.FieldMissing };
      } else if (!this.childIs('version', 'string')) {
        yield { message: i`version should be of type 'string', found '${this.kind('version')}'`, range: this.sourcePosition('version'), category: ErrorKind.IncorrectType };
      }
      if (this.childIs('summary', 'string') === false) {
        yield { message: i`summary should be of type 'string', found '${this.kind('summary')}'`, range: this.sourcePosition('summary'), category: ErrorKind.IncorrectType };
      }
      if (this.childIs('description', 'string') === false) {
        yield { message: i`description should be of type 'string', found '${this.kind('description')}'`, range: this.sourcePosition('description'), category: ErrorKind.IncorrectType };
      }
      if (this.childIs('options', 'sequence') === false) {
        yield { message: i`options should be a sequence, found '${this.kind('options')}'`, range: this.sourcePosition('options'), category: ErrorKind.IncorrectType };
      }
    }

    if (this.document.has('contacts')) {
      for (const each of this.contacts.values) {
        yield* each.validate();
      }
    }

    const set = new Set<string>();
    for (const [mediaQuery, demandBlock] of this.conditionalDemands) {
      if (set.has(mediaQuery)) {
        yield { message: i`Duplicate keys detected in manifest: '${mediaQuery}'`, range: demandBlock, category: ErrorKind.DuplicateKey };
      }

      set.add(mediaQuery);
      yield* demandBlock.validate();
    }
    yield* this.conditionalDemands.validate();
    yield* this.install.validate();
    yield* this.registries.validate();
    yield* this.contacts.validate();
    yield* this.exports.validate();
    yield* this.globalSettings.validate();
    yield* this.requires.validate();
    yield* this.seeAlso.validate();
  }

  normalize() {
    if (!this.node) { return; }
    if (this.document.has('info')) {
      this.setMember('id', this.#info.id);
      this.setMember('version', this.#info.version);
      this.setMember('summary', this.#info.summary);
      this.setMember('description', this.#info.description);
      const maybeOptions = this.#info.options.node?.items;
      if (maybeOptions) {
        for (const option of maybeOptions) {
          this.#options.set(option.value, true);
        }
      }

      this.setMember('priority', this.#info.priority);
      this.node.delete('info');
    }
  }

  /** @internal */override assert(recreateIfDisposed = false, node = this.node): asserts this is Yaml<YAMLDictionary> & { node: YAMLDictionary } {
    if (!isMap(this.node)) {
      this.document = parseDocument('{}\n', { prettyErrors: false, lineCounter: this.lineCounter, strict: true });
      this.node = <YAMLMap<string, any>><any>this.document.contents;
    }
  }
}
