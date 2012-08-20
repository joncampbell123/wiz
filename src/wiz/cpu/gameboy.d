module wiz.cpu.gameboy;

import wiz.lib;
import wiz.cpu.lib;

private
{
    enum ArgumentType
    {
        Undefined,
        Immediate,
        Indirection,
        IndirectionInc,
        IndirectionDec,
        PositiveIndex,
        NegativeIndex,
        BitIndex,
        A,
        B,
        C,
        D,
        E,
        F,
        H,
        L,
        AF,
        BC,
        DE,
        HL,
        SP,
        Carry,
        Zero,
        Interrupt,
    }

    class Argument
    {
        ArgumentType type;
        ast.Expression immediate;
        Argument base;

        this(ArgumentType type)
        {
            this.type = type;
        }

        this(ArgumentType type, ast.Expression immediate)
        {
            this(type);
            this.immediate = immediate;
        }

        this(ArgumentType type, Argument base)
        {
            this(type);
            this.base = base;
        }

        this(ArgumentType type, ast.Expression immediate, Argument base)
        {
            this(type);
            this.immediate = immediate;
            this.base = base;
        }
    }

    class Builtin : sym.Definition
    {
        ArgumentType type;

        this(ArgumentType type)
        {
            super(new ast.BuiltinDecl());
            this.type = type;
        }
    }

    Argument buildIndirection(compile.Program program, ast.Expression root)
    {
        if(auto attr = cast(ast.Attribute) root)
        {
            auto def = compile.resolveAttribute(program, attr);
            if(auto builtin = cast(Builtin) def)
            {
                return new Argument(ArgumentType.Indirection, new Argument(builtin.type));
            }
        }
        else if(auto prefix = cast(ast.Prefix) root)
        {
            if(prefix.type == parse.Prefix.Indirection)
            {
                error("double-indirection is not supported on the gameboy", prefix.location);
                return null;
            }
        }
        else if(auto postfix = cast(ast.Postfix) root)
        {
            ArgumentType argumentType;
            final switch(postfix.type)
            {
                case parse.Postfix.Inc: argumentType = ArgumentType.IndirectionInc; break;
                case parse.Postfix.Dec: argumentType = ArgumentType.IndirectionDec; break;
            }
            if(auto attr = cast(ast.Attribute) postfix.operand)
            {
                auto def = compile.resolveAttribute(program, attr);
                if(auto builtin = cast(Builtin) def)
                {
                    if(builtin.type == ArgumentType.HL)
                    {
                        return new Argument(argumentType, new Argument(builtin.type));
                    }
                }
            }
            error(
                "operator " ~ parse.getPostfixName(postfix.type)
                ~ " on indirected operand is not supported (only 'hl' is valid)",
                postfix.location
            );
            return null;
        }
        else if(auto infix = cast(ast.Infix) root)
        {
            Builtin registerLeft;
            Builtin registerRight;

            if(infix.type == parse.Infix.Colon)
            {
                if(auto attr = cast(ast.Attribute) infix.right)
                {
                    auto def = compile.resolveAttribute(program, attr);
                    if(auto builtin = cast(Builtin) def)
                    {
                        if(auto prefix = cast(ast.Prefix) infix.left)
                        {
                            if(prefix.type == parse.Infix.Sub)
                            {
                                return new Argument(ArgumentType.NegativeIndex, prefix.operand, new Argument(builtin.type));
                            }
                        }
                        return new Argument(ArgumentType.PositiveIndex, infix.left, new Argument(builtin.type));
                    }
                }
                error(
                    "index operator ':' must have register as a right-hand term",
                    infix.location
                );
                return null;
            }
        }
        return new Argument(ArgumentType.Indirection, new Argument(ArgumentType.Immediate, root));
    }

    Argument buildArgument(compile.Program program, ast.Expression root)
    {
        uint v;
        if(auto attr = cast(ast.Attribute) root)
        {
            auto def = compile.resolveAttribute(program, attr);
            if(auto builtin = cast(Builtin) def)
            {
                return new Argument(builtin.type);
            }
        }
        else if(auto prefix = cast(ast.Prefix) root)
        {
            if(prefix.type == parse.Token.LBracket)
            {
                return buildIndirection(program, prefix);
            }
        }
        else if(auto infix = cast(ast.Infix) root)
        {
            if(infix.type == parse.Token.At)
            {
                if(auto attr = cast(ast.Attribute) infix.left)
                {
                    auto def = compile.resolveAttribute(program, attr);
                    if(auto builtin = cast(Builtin) def)
                    {
                        return new Argument(ArgumentType.BitIndex, infix.right, new Argument(builtin.type));
                    }
                }
            }
        }
        return new Argument(ArgumentType.Immediate, root);
    }
}

class GameboyPlatform : Platform
{
    sym.Definition[string] builtins()
    {
        return [
            "a": new Builtin(ArgumentType.A),
            "b": new Builtin(ArgumentType.B),
            "c": new Builtin(ArgumentType.C),
            "d": new Builtin(ArgumentType.D),
            "e": new Builtin(ArgumentType.E),
            "f": new Builtin(ArgumentType.F),
            "h": new Builtin(ArgumentType.H),
            "l": new Builtin(ArgumentType.L),
            "af": new Builtin(ArgumentType.AF),
            "bc": new Builtin(ArgumentType.BC),
            "de": new Builtin(ArgumentType.DE),
            "hl": new Builtin(ArgumentType.HL),
            "sp": new Builtin(ArgumentType.SP),
            "carry": new Builtin(ArgumentType.Carry),
            "zero": new Builtin(ArgumentType.Zero),
            "interrupt": new Builtin(ArgumentType.Interrupt),
        ];
    }

    ubyte[] generateCommand(compile.Program program, ast.Command stmt)
    {
        switch(stmt.type)
        {
            case parse.Keyword.Push:
                auto argument = buildArgument(program, stmt.argument);
                switch(argument.type)
                {
                    case ArgumentType.AF: return [0xF5];
                    case ArgumentType.BC: return [0xC5];
                    case ArgumentType.DE: return [0xD5];
                    case ArgumentType.HL: return [0xE5];
                    default: return [];
                }
            default:
                return [];
        }
    }
    
    ubyte[] generateJump(compile.Program program, ast.Jump stmt)
    {
        switch(stmt.type)
        {
            case parse.Keyword.Goto:
                auto argument = buildArgument(program, stmt.destination);
                switch(argument.type)
                {
                    case ArgumentType.Immediate:
                        uint address;
                        if(!compile.foldConstExpr(program, argument.immediate, address))
                        {
                            // For now, sub a placeholder expression.
                            // At a later pass, we will ensure that the address is resolvable.
                            address = 0xFACE;
                        }
                        if(stmt.condition is null)
                        {
                            return [0xC3, address & 0xFF, (address >> 8) & 0xFF];
                        }
                        else
                        {
                            auto cond = stmt.condition;
                            if(cond.attr is null)
                            {
                                final switch(cond.branch)
                                {
                                    case parse.Branch.Equal:
                                        break;
                                    case parse.Branch.NotEqual:
                                        break;
                                    case parse.Branch.Less:
                                        break;
                                    case parse.Branch.Greater:
                                        break;
                                    case parse.Branch.LessEqual:
                                        break;
                                    case parse.Branch.GreaterEqual:
                                        break;
                                }
                            }
                            else
                            {
                                auto attr = cond.attr;
                                auto def = compile.resolveAttribute(program, attr);
                                if(auto builtin = cast(Builtin) def)
                                {
                                    switch(builtin.type)
                                    {
                                        case ArgumentType.Carry:
                                            return [
                                                cond.negated ? 0xD2 : 0xDA,
                                                address & 0xFF,
                                                (address >> 8) & 0xFF
                                            ];
                                        case ArgumentType.Zero:
                                            return [
                                                cond.negated ? 0xC2 : 0xCA,
                                                address & 0xFF,
                                                (address >> 8) & 0xFF
                                            ];
                                        default:
                                    }
                                }
                                error(
                                    "unrecognized condition '"
                                    ~ attr.fullName()
                                    ~ "' used in 'when' clause",
                                    cond.location
                                );
                                return [];
                            }
                        }

                    case ArgumentType.HL:
                        if(stmt.condition is null)
                        {
                            return [0xE9];
                        }
                        else
                        {
                            error("'goto hl' does not support 'when' clause", stmt.condition.location);
                        }
                    default: return [];
                }
            case parse.Keyword.Call:
                return [];
            case parse.Keyword.Return:
                return [];
            case parse.Keyword.Resume:
                return [0xD9];
            case parse.Keyword.Break:
                return [];
            case parse.Keyword.Continue:
                return [];
            case parse.Keyword.While:
                return [];
            case parse.Keyword.Until:
                return [];
            case parse.Keyword.Abort:
                return [0x40];
            case parse.Keyword.Sleep:
                return [0x76];
            case parse.Keyword.Suspend:
                return [0x10, 0x00];
            case parse.Keyword.Nop:
                return [0x00];
            default:
                return [];
        }
    }

    ubyte[] generateAssignment(compile.Program program, ast.Assignment stmt)
    {
        return [];
    }
}