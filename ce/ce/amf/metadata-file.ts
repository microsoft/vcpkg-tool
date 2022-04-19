// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { extname } from 'path';
import { Document, isMap, LineCounter, parseDocument, YAMLMap } from 'yaml';
import { Activation } from '../artifacts/activation';
import { Registry } from '../artifacts/registry';
import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { Profile } from '../interfaces/metadata/metadata-format';
import { ValidationError } from '../interfaces/validation-error';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { BaseMap } from '../yaml/BaseMap';
import { toYAML } from '../yaml/yaml';
import { Yaml, YAMLDictionary } from '../yaml/yaml-types';
import { Contacts } from './contact';
import { DemandBlock, Demands } from './demands';
import { DocumentContext } from './document-context';
import { GlobalSettings } from './global-settings';
import { Info } from './info';
import { Registries } from './registries';


export class MetadataFile extends BaseMap implements Profile {
  readonly context: DocumentContext;
  session!: Session;

  private constructor(protected document: Document.Parsed, public readonly filename: string, public lineCounter: LineCounter, public readonly registry: Registry | undefined) {
    super(<YAMLMap<string, any>><any>document.contents);
    this.context = <DocumentContext>{
      filename,
      lineCounter,
    };
  }

  async init(session: Session): Promise<MetadataFile> {
    this.context.session = session;
    this.context.file = session.parseUri(this.context.filename);
    this.context.folder = this.context.file.parent;
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
    return new MetadataFile(doc, filename, lc, registry).init(session);
  }

  info = new Info(undefined, this, 'info');

  contacts = new Contacts(undefined, this, 'contacts');
  registries = new Registries(undefined, this, 'registries');
  globalSettings = new GlobalSettings(undefined, this, 'global');

  // rather than re-implement it, use encapsulatiob with a demand block
  private demandBlock = new DemandBlock(this.node, undefined);

  get error(): string | undefined { return this.demandBlock.error; }
  set error(value: string | undefined) { this.demandBlock.error = value; }

  get warning(): string | undefined { return this.demandBlock.warning; }
  set warning(value: string | undefined) { this.demandBlock.warning = value; }

  get message(): string | undefined { return this.demandBlock.message; }
  set message(value: string | undefined) { this.demandBlock.message = value; }

  get seeAlso() { return this.demandBlock.seeAlso; }
  get requires() { return this.demandBlock.requires; }
  get settings() { return this.demandBlock.settings; }
  get install() { return this.demandBlock.install; }
  get unless() { return this.demandBlock.unless; }

  setActivation(activation: Activation): void {
    this.demandBlock.setActivation(activation);
  }

  conditionalDemands = new Demands(undefined, this, 'demands');

  get isFormatValid(): boolean {
    return this.document.errors.length === 0;
  }

  get content() {
    return toYAML(this.document.toString());
  }

  async save(uri: Uri = this.context.file): Promise<void> {
    // check the filename, and select the format.
    let content = '';

    switch (extname(uri.path).toLowerCase()) {
      case '.yaml':
      case '.yml':
        // format as yaml
        content = this.content;
        break;

      case '.json':
        content = JSON.stringify(this.document.toJSON(), null, 2);
        break;
      default:
        throw new Error(`Unsupported file type ${extname(uri.path)}`);
    }
    if (!content || content === 'null') {
      content = '{\n}';
    }
    await uri.writeUTF8(content);
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

  get isValid(): boolean {
    return this.validationErrors.length === 0;
  }

  #validationErrors!: Array<string>;
  get validationErrors(): Array<string> {
    if (this.#validationErrors) {
      return this.#validationErrors;
    }

    const errs = new Set<string>();
    for (const { message, range, rangeOffset, category } of this.validate()) {
      const r = Array.isArray(range) ? range : range?.sourcePosition();

      const { line, column } = this.positionAt(r, rangeOffset);
      errs.add(this.formatMessage(category, message, line, column));
    }
    this.#validationErrors = [...errs];
    return this.#validationErrors;
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
  override *validate(): Iterable<ValidationError> {
    yield* super.validate();

    // verify that we have info
    if (!this.document.has('info')) {
      yield { message: i`Missing section '${'info'}'`, range: this, category: ErrorKind.SectionNotFound };
    } else {
      yield* this.info.validate();
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
    yield* this.settings.validate();
    yield* this.globalSettings.validate();
    yield* this.requires.validate();
    yield* this.seeAlso.validate();
  }

  /** @internal */override assert(recreateIfDisposed = false, node = this.node): asserts this is Yaml<YAMLDictionary> & { node: YAMLDictionary } {
    if (!isMap(this.node)) {
      this.document = parseDocument('{\n}', { prettyErrors: false, lineCounter: this.context.lineCounter, strict: true });
      this.node = <YAMLMap<string, any>><any>this.document.contents;
    }
  }
}
