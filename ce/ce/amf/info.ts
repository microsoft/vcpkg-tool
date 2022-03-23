// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { Info as IInfo } from '../interfaces/metadata/info';
import { ValidationError } from '../interfaces/validation-error';
import { Entity } from '../yaml/Entity';
import { Flags } from '../yaml/Flags';


export class Info extends Entity implements IInfo {
  get version(): string { return this.asString(this.getMember('version')) || ''; }
  set version(value: string) { this.setMember('version', value); }

  get id(): string { return this.asString(this.getMember('id')) || ''; }
  set id(value: string) { this.setMember('id', value); }

  get summary(): string | undefined { return this.asString(this.getMember('summary')); }
  set summary(value: string | undefined) { this.setMember('summary', value); }

  get priority(): number | undefined { return this.asNumber(this.getMember('priority')) || 0; }
  set priority(value: number | undefined) { this.setMember('priority', value); }

  get description(): string | undefined { return this.asString(this.getMember('description')); }
  set description(value: string | undefined) { this.setMember('description', value); }

  private flags = new Flags(undefined, this, 'options');

  get dependencyOnly(): boolean { return this.flags.has('dependencyOnly'); }
  set dependencyOnly(value: boolean) { this.flags.set('dependencyOnly', value); }

  /** @internal */
  override *validate(): Iterable<ValidationError> {
    yield* super.validate();

    if (!this.has('id')) {
      yield { message: i`Missing identity '${'info.id'}'`, range: this, category: ErrorKind.FieldMissing };
    } else if (!this.is('id', 'string')) {
      yield { message: i`info.id should be of type 'string', found '${this.kind('id')}'`, range: this.sourcePosition('id'), category: ErrorKind.IncorrectType };
    }

    if (!this.has('version')) {
      yield { message: i`Missing version '${'info.version'}'`, range: this, category: ErrorKind.FieldMissing };
    } else if (!this.is('version', 'string')) {
      yield { message: i`info.version should be of type 'string', found '${this.kind('version')}'`, range: this.sourcePosition('version'), category: ErrorKind.IncorrectType };
    }
    if (this.is('summary', 'string') === false) {
      yield { message: i`info.summary should be of type 'string', found '${this.kind('summary')}'`, range: this.sourcePosition('summary'), category: ErrorKind.IncorrectType };
    }
    if (this.is('description', 'string') === false) {
      yield { message: i`info.description should be of type 'string', found '${this.kind('description')}'`, range: this.sourcePosition('description'), category: ErrorKind.IncorrectType };
    }
    if (this.is('options', 'sequence') === false) {
      yield { message: i`info.options should be a sequence, found '${this.kind('options')}'`, range: this.sourcePosition('options'), category: ErrorKind.IncorrectType };
    }
  }
}
