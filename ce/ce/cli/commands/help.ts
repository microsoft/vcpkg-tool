// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Argument } from '../argument';
import { Command } from '../command';
import { blank, cli } from '../constants';
import { command as formatCommand, heading, hint } from '../format';
import { error, indent, log } from '../styling';

class CommandName extends Argument {
  argument = 'command';

  get help() {
    return [
      i`the name of the command for which you want help`
    ];
  }
}

/**@internal */
export class HelpCommand extends Command {
  readonly command = 'help';
  readonly aliases = [];
  seeAlso = [];
  commandName: CommandName = new CommandName(this);

  get argumentsHelp() {
    return [indent(i` <${this.commandName.argument}> : ${this.commandName.help.join(' ')}`)];
  }

  get summary() {
    return i`get help on ${cli} or one of the commands`;
  }

  get description() {
    return [
      i`Gets detailed help on ${cli}, or one of the commands`,
      blank,
      i`Arguments:`
    ];
  }

  override async run() {
    const cmd = ['-h', '-help', '-?', '/?'].find(each => (this.commandLine.inputs.indexOf(each) > -1)) ? this.commandLine.inputs[0] : this.commandLine.inputs[1];
    // did they ask for help on a command?

    if (cmd) {
      const target = this.commandLine.commands.find(each => each.command === cmd);
      if (target) {
        log(target.help.join('\n'));
        log(blank);
        return true;
      }

      // I don't know the command
      error(i`Unrecognized command '${cmd}'`);
      log(hint(i`Use ${formatCommand(`${cli} ${this.command}`)} to get the list of available commands`));
      return false;
    }

    // general help. return the general help info

    log(heading(i`Usage`, 2));
    log(blank);
    log(indent(i`${cli} COMMAND <arguments> [--switches]`));
    log(blank);

    log(heading(i`Available ${cli} commands:`, 2));
    log(blank);
    const max = Math.max(...this.commandLine.commands.map(each => each.command.length));
    for (const command of this.commandLine.commands) {
      if (command.command.startsWith('z-')) {
        // don't show internal commands
        continue;
      }
      log(indent(i`${formatCommand(command.command.padEnd(max))} : ${command.summary}`));
    }

    log(blank);

    return true;
  }
}
