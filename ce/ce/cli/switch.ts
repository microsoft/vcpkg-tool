// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { i } from '../i18n';
import { Command } from './command';
import { Help } from './command-line';
import { cmdSwitch } from './format';


export abstract class Switch implements Help {
  readonly abstract switch: string;
  readonly title = '';
  readonly abstract help: Array<string>;
  readonly required: boolean;
  readonly multipleAllowed: boolean;

  constructor(protected command: Command, options?: { multipleAllowed?: boolean, required?: boolean }) {
    command.switches.push(this);
    this.multipleAllowed = options?.multipleAllowed || false;
    this.required = options?.required || false;
  }

  get valid() {
    return this.required || this.active;
  }

  #values?: Array<string>;
  get values() {
    return this.#values || (this.#values = this.command.commandLine.claim(this.switch) || []);
  }

  get value(): any | undefined {
    const v = this.values;
    strict.ok(v.length < 2, i`Expected a single value for ${cmdSwitch(this.switch)} - found multiple`);
    return v[0];
  }

  get requiredValue(): string {
    const v = this.values;
    strict.ok(v.length == 1 && v[0], i`Expected a single value for '--${this.switch}'.`);
    return v[0];
  }

  get active(): boolean {
    const v = this.values;
    return !!v && v.length > 0 && v[0] !== 'false';
  }
}
