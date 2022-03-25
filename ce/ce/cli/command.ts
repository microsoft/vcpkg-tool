// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../i18n';
import { Argument } from './argument';
import { CommandLine, Help } from './command-line';
import { blank, cli } from './constants';
import { cmdSwitch, command, heading, optional } from './format';
import { Switch } from './switch';
import { Debug } from './switches/debug';
import { Force } from './switches/force';

/** @internal */

export abstract class Command implements Help {
  readonly abstract command: string;
  readonly abstract argumentsHelp: Array<string>;

  readonly switches = new Array<Switch>();
  readonly arguments = new Array<Argument>();

  readonly abstract seeAlso: Array<Help>;
  readonly abstract aliases: Array<string>;

  abstract get summary(): string;
  abstract get description(): Array<string>;

  readonly force = new Force(this);
  readonly debug = new Debug(this);

  get synopsis(): Array<string> {
    return [
      heading(i`Synopsis`, 2),
      ` ${command(`${cli} ${this.command} ${this.arguments.map(each => `<${each.argument}>`).join(' ')}`)}${this.switches.flatMap(each => optional(`[--${each.switch}]`)).join(' ')}`,
    ];
  }

  get title() {
    return `${cli} ${this.command}`;
  }

  constructor(public commandLine: CommandLine) {
    commandLine.addCommand(this);
  }

  get inputs() {
    return this.commandLine.inputs.slice(1);
  }

  get help() {
    return [
      heading(this.title),
      blank,
      this.summary,
      blank,
      ...this.synopsis,
      blank,
      heading(i`Description`, 2),
      blank,
      ...this.description,
      ...this.argumentsHelp,
      ...(this.switches.length ? [
        blank,
        heading(i`Switches`, 2),
        blank,
        ...this.switches.flatMap(each => ` ${cmdSwitch(each.switch)}: ${each.help.join(' ')}`)
      ] : []),
      ...(this.seeAlso.length ? [
        heading(i`See Also`, 2),
        ...this.seeAlso.flatMap(each => each.title)
      ] : []),
    ];
  }

  async run() {
    // do something
    return true;
  }

}
