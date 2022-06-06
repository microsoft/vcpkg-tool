import { spawn } from "child_process";
import { Session } from "./session";

/** @internal */
export class Vcpkg {
    constructor(private readonly session: Session) {}

    fetch(fetchKey: string): Promise<string | undefined> {
        return this.runVcpkg(['fetch', fetchKey, '--x-stderr-status']).then((results) => {
            if (results === undefined) {
                if (fetchKey === 'git') {
                    this.session.channels.warning('failed to fetch git, falling back to attempting to use git from the PATH');
                    return 'git';
                }

                return results;
            }

            return results.trimEnd();
        });
    }

    private runVcpkg(args: string[]): Promise<string | undefined> {
        return new Promise((accept, reject) => {
            if (!this.session.vcpkgCommand) {
                accept(undefined);
                return;
            }

            const subproc = spawn(this.session.vcpkgCommand, args, {stdio: ['ignore', 'pipe', 'pipe']});
            let result = '';
            subproc.stdout.on('data', (chunk) => { result += chunk; });
            subproc.stderr.pipe(process.stdout);
            subproc.on('error', (err) => { reject(err); });
            subproc.on('close', (code, signal) => {
                if (code === 0) { accept(result); }
                accept(undefined);
            });
        });
    }
}
