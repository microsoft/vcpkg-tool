// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary } from '../interfaces/collections';
import { Contact as IContact } from '../interfaces/metadata/contact';
import { ValidationError } from '../interfaces/validation-error';
import { Entity } from '../yaml/Entity';
import { EntityMap } from '../yaml/EntityMap';
import { Strings } from '../yaml/strings';
import { Yaml, YAMLDictionary } from '../yaml/yaml-types';

export class Contact extends Entity implements IContact {
  get email(): string | undefined { return this.asString(this.getMember('email')); }
  set email(value: string | undefined) { this.setMember('email', value); }

  readonly roles = new Strings(undefined, this, 'role');
  /** @internal */
  override *validate(): Iterable<ValidationError> {
    yield* super.validate();
  }
}

export class Contacts extends EntityMap<YAMLDictionary, Contact> implements Dictionary<IContact> {
  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(Contact, node, parent, key);
  }
  /** @internal */
  override *validate(): Iterable<ValidationError> {
    yield* super.validate();
    if (this.exists()) {
      for (const [key, contact] of this) {
        yield* contact.validate();
      }
    }
  }
}
