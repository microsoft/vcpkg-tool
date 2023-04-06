// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../i18n';
import { ErrorKind } from '../interfaces/error-kind';
import { Validation } from '../interfaces/validation';
import { ValidationMessage } from '../interfaces/validation-message';
import { Entity } from '../yaml/Entity';
import { Options } from '../yaml/Options';


export class Info extends Entity implements Validation {
  // See corresponding properties in MetadataFile
  get id(): string { return this.asString(this.getMember('id')) || ''; }

  get version(): string { return this.asString(this.getMember('version')) || ''; }

  get summary(): string | undefined { return this.asString(this.getMember('summary')); }

  get description(): string | undefined { return this.asString(this.getMember('description')); }

  readonly options = new Options(undefined, this, 'options');

  get priority(): number { return this.asNumber(this.getMember('priority')) || 0; }

  /** @internal */
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys(['version', 'id', 'summary', 'priority', 'description', 'options']);

    if (!this.has('id')) {
      yield { message: i`Missing identity '${'info.id'}'`, range: this, category: ErrorKind.FieldMissing };
    } else if (!this.childIs('id', 'string')) {
      yield { message: i`info.id should be of type 'string', found '${this.kind('id')}'`, range: this.sourcePosition('id'), category: ErrorKind.IncorrectType };
    }

    if (!this.has('version')) {
      yield { message: i`Missing version '${'info.version'}'`, range: this, category: ErrorKind.FieldMissing };
    } else if (!this.childIs('version', 'string')) {
      yield { message: i`info.version should be of type 'string', found '${this.kind('version')}'`, range: this.sourcePosition('version'), category: ErrorKind.IncorrectType };
    }
    if (this.childIs('summary', 'string') === false) {
      yield { message: i`info.summary should be of type 'string', found '${this.kind('summary')}'`, range: this.sourcePosition('summary'), category: ErrorKind.IncorrectType };
    }
    if (this.childIs('description', 'string') === false) {
      yield { message: i`info.description should be of type 'string', found '${this.kind('description')}'`, range: this.sourcePosition('description'), category: ErrorKind.IncorrectType };
    }
    if (this.childIs('options', 'sequence') === false) {
      yield { message: i`info.options should be a sequence, found '${this.kind('options')}'`, range: this.sourcePosition('options'), category: ErrorKind.IncorrectType };
    }
  }
}
