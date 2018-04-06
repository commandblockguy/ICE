#!/usr/bin/env python3
# -*- coding: latin-1 -*-

import re
import irc.bot

class IceBot(irc.bot.SingleServerIRCBot):
    functionPattern = re.compile(r"(\w+)\((\w+)(,[^)]*|)\)?")
    namePattern = re.compile(r"\w+")
    tagPattern = re.compile(r"[<>][a-z]")
    functions = {}
    with open("icebot.txt") as data:
        for line in data:
            syntax, name, description = line[:-1].split('\t')
            kind, index, arguments = functionPattern.fullmatch(syntax).groups()
            arguments = arguments.split(',')
            functions.setdefault(kind, {})[int(index)] = (
                name.casefold(), ">b%s<b | >b>n%s(<s<b%s%s>b>n)<s<b | %s" %
                (name, kind, index, '>b>n,<s<b'.join(arguments), description))

    @staticmethod
    def __distance(a, b):
        bests = [[2, 1] + [None] * len(b), [*range(0, len(b) + 2)], [1, *range(0, len(b) + 1)]]
        for i in range(len(a)):
            for j in range(len(b)):
                bests[0][j + 2] = min(bests[-1][j + 2] + 1, bests[0][j + 1] + 1,
                                      bests[-1][j + 1] + (0 if a[i] == b[j] else 2),
                                      bests[-2][j] + (1 if i and j and a[i-1] == b[j]
                                                      and a[i] == b[j-1] else 4))
            bests = bests[1:] + bests[:1]
            bests[0][:2] = i + 3, i + 2
        return bests[-1][-1]

    def __init__(self, channel, nickname, server, port=6667):
        irc.bot.SingleServerIRCBot.__init__(
            self, [(server, port)], nickname, nickname)
        self.channel = channel

    def on_nicknameinuse(self, c, e):
        c.nick(c.get_nickname() + "_")

    def on_welcome(self, c, e):
        c.join(self.channel)

    def on_privmsg(self, c, e):
        c = self.connection
        request = e.arguments[0].lower()

        if request.startswith("~ice "):
            request = request[5:]

        c.privmsg(e.source.nick, self.do_ice(e, request))

    def on_pubmsg(self, c, e):
        c = self.connection
        request = e.arguments[0].lower()
        nick = e.source.nick

        if nick == "saxjax":
            offset = request.find("]")
            request = request[offset+2:]

        if request.startswith("~ice "):
            c.privmsg(e.target, self.do_ice(e, request[5:]))

    def do_ice(self, e, cmd):
        output = "[>b>oICE<s<b] "

        try:
            functionMatch = self.functionPattern.fullmatch(cmd)
            cmd = cmd.casefold().strip()
            if functionMatch:
                output += self.functions[functionMatch.group(1).lower()][int(functionMatch.group(2))][1]
            elif self.namePattern.fullmatch(cmd):
                best = min((self.__distance(function[0], cmd), *function) for kind in self.functions.values() for function in kind.values())
                if 2*best[0] > len(best[1]):
                    raise KeyError()
                output += best[2]
            else:
                raise ValueError()
        except KeyError:
            output += ">runknown function<s"
        except ValueError:
            output += ">rinvalid syntax<s: use \"~ice det(XX)\" or \"~ice sum(XX)\" or \"~ice <function>\" to search for a function"

        if e.target != "#cemetech":
            for tag, irc in (">b", "\x02"), (">n", "\x0302"), (">r", "\x0304"), (">o", "\x0307"), (">g", "\x0315"), ("<b", "\x02"), ("<s", "\x03"):
                output = output.replace(tag, irc)
        else:
            output = self.tagPattern.sub("", output)

        return output

def main():
    server = "efnet.port80.se"
    port = 6667
    channel = "#icedev"
    nickname = "ICEbot"

    bot = IceBot(channel, nickname, server, port)
    bot.start()

if __name__ == "__main__":
    main()
