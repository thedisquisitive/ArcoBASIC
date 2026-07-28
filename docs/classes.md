# Classes in ArcoBASIC

Status: alpha

ArcoBASIC classes are lightweight object blueprints. They support fields,
constructors, methods, inheritance, shared members, access modifiers,
interfaces, abstract methods, and runtime-checked `AS Type` annotations.

The current implementation is interpreted and runtime-checked. The syntax is
intended to be stable enough for alpha scripts, but the object model may still
gain stricter diagnostics later.

## Minimal class

```basic
CLASS Person
    Name AS String = "Ada"
    Age AS Number = 36

    FUNCTION Label() AS String
        RETURN SELF.Name + ":" + STRING(SELF.Age)
    END FUNCTION
END CLASS

person = Person()
PRINT person.Name
PRINT person.Label()
```

Create an instance by calling the class name like a function:

```basic
person = Person()
```

Do not use `Person.NEW()` for the current alpha runtime.

## Fields

Fields are declared directly inside the class body.

```basic
CLASS Counter
    Value AS Number = 0
    Label AS String
END CLASS
```

Fields may have defaults. If a typed field has no default, it starts as `NULL`.
Later non-null assignments must match the declared type.

```basic
counter = Counter()
counter.Value = 10      ' ok
counter.Label = "main"  ' ok
counter.Value = "bad"   ' runtime error
```

Untyped fields are allowed:

```basic
CLASS Bag
    Anything = NULL
END CLASS
```

## Constructors

Use `CONSTRUCTOR ... END CONSTRUCTOR` for initialization.

```basic
CLASS Counter
    Value AS Number = 0

    CONSTRUCTOR(start AS Number)
        SELF.Value = start
    END CONSTRUCTOR
END CLASS

counter = Counter(10)
PRINT counter.Value
```

Constructors may have typed parameters and default parameter values. They do
not declare return types.

The older `Init` method still works as a compatibility hook:

```basic
CLASS Stamp
    Value AS String = "unset"

    FUNCTION Init()
        SELF.Value = "ready"
    END FUNCTION
END CLASS
```

Prefer `CONSTRUCTOR` in new code.

## Methods and `SELF`

Methods are ordinary functions inside a class. Use `SELF` to access the current
instance.

```basic
CLASS Counter
    Value AS Number = 0

    FUNCTION Increment(amount AS Number = 1) AS Number
        SELF.Value = SELF.Value + amount
        RETURN SELF.Value
    END FUNCTION
END CLASS

counter = Counter()
PRINT counter.Increment()
PRINT counter.Increment(5)
```

Methods may have:

* typed parameters
* default parameters
* typed return values

Return types are checked when the method returns.

## Runtime type annotations

Use `AS Type` for runtime checks.

```basic
FUNCTION DoubleIt(value AS Number) AS Number
    RETURN value * 2
END FUNCTION
```

Supported core type names:

* `String`
* `Number`
* `Boolean` or `Bool`
* `Array` or `List`
* `Object`
* `Null` or `Nothing`
* `Any`, `Variant`, or `Value`

Class and interface names can also be used as types:

```basic
FUNCTION Describe(animal AS Animal) AS String
    RETURN animal.Speak()
END FUNCTION
```

Type checks are runtime checks. They do not currently make ArcoBASIC a static
typed language.

## Inheritance

Use `EXTENDS` to inherit fields and methods.

```basic
CLASS Animal
    Name AS String = "unknown"

    CONSTRUCTOR(name AS String)
        SELF.Name = name
    END CONSTRUCTOR

    FUNCTION Speak() AS String
        RETURN SELF.Name + " makes a sound"
    END FUNCTION
END CLASS

CLASS Cat EXTENDS Animal
    Lives AS Number = 9

    FUNCTION Speak() AS String
        RETURN SUPER.Speak() + " and meows"
    END FUNCTION
END CLASS

cat = Cat("Miso")
PRINT cat.Name
PRINT cat.Lives
PRINT cat.Speak()
```

Child classes inherit parent fields and fall back to parent methods when a
method is not overridden.

Use `SUPER.MethodName(...)` from an overridden method to call the parent method.

## Class inspection

Use `CLASSOF` and `ISA`.

```basic
PRINT CLASSOF(cat)
PRINT ISA(cat, "Cat")
PRINT ISA(cat, "Animal")
```

Example output:

```text
Cat
TRUE
TRUE
```

## Shared fields and methods

Use `SHARED` for class-level members.

```basic
CLASS Ticket
    SHARED NextId AS Number = 100
    Id AS Number = 0

    SHARED FUNCTION Issue() AS Number
        Ticket.NextId = Ticket.NextId + 1
        RETURN Ticket.NextId
    END FUNCTION

    CONSTRUCTOR()
        SELF.Id = Ticket.Issue()
    END CONSTRUCTOR
END CLASS

a = Ticket()
b = Ticket()

PRINT Ticket.NextId
PRINT a.Id
PRINT b.Id
```

Shared members are accessed through `ClassName.Member`.

## Access modifiers

Class fields and methods are `PUBLIC` by default.

Available modifiers:

* `PUBLIC`
* `PROTECTED`
* `PRIVATE`

```basic
CLASS Vault
    PRIVATE Secret AS String = "alpha"

    PRIVATE FUNCTION Reveal() AS String
        RETURN SELF.Secret
    END FUNCTION

    PUBLIC FUNCTION Open() AS String
        RETURN SELF.Reveal()
    END FUNCTION
END CLASS

vault = Vault()
PRINT vault.Open()
```

`PRIVATE` members can only be accessed by methods on the declaring class.

`PROTECTED` members can be accessed by the declaring class and subclasses.

```basic
CLASS Machine
    PROTECTED Serial AS String = "M-7"

    PROTECTED FUNCTION ProtectedLabel() AS String
        RETURN SELF.Serial + ":core"
    END FUNCTION
END CLASS

CLASS Robot EXTENDS Machine
    PUBLIC FUNCTION RobotLabel() AS String
        RETURN SELF.ProtectedLabel() + ":robot"
    END FUNCTION
END CLASS
```

## Interfaces

Interfaces declare method requirements.

```basic
INTERFACE Writer
    FUNCTION Write(text AS String) AS String
    FUNCTION Flush() AS String
END INTERFACE
```

Use `IMPLEMENTS` on a class:

```basic
CLASS BufferWriter IMPLEMENTS Writer
    Text AS String = ""

    FUNCTION Write(text AS String) AS String
        SELF.Text = SELF.Text + text
        RETURN SELF.Text
    END FUNCTION

    FUNCTION Flush() AS String
        RETURN SELF.Text
    END FUNCTION
END CLASS
```

At class load time, ArcoBASIC checks that required interface methods exist.
When the interface declares types, ArcoBASIC also checks:

* parameter count
* parameter types
* return type

Use the `IMPLEMENTS` helper to inspect an object:

```basic
writer = BufferWriter()
PRINT IMPLEMENTS(writer, "Writer")
```

## Abstract methods

Use `ABSTRACT FUNCTION` to require subclasses to implement a method.

```basic
CLASS Shape
    ABSTRACT FUNCTION Area() AS Number

    FUNCTION Describe() AS String
        RETURN "shape"
    END FUNCTION
END CLASS

CLASS Square EXTENDS Shape
    Side AS Number = 4

    FUNCTION Area() AS Number
        RETURN SELF.Side * SELF.Side
    END FUNCTION
END CLASS

square = Square()
PRINT square.Describe()
PRINT square.Area()
```

A class with unresolved abstract methods cannot be instantiated.

```basic
bad = Shape()  ' runtime error
```

## Error handling example

Use `TRY` / `CATCH` to handle runtime type or access errors.

```basic
CLASS Box
    Value AS Number = 0
END CLASS

box = Box()

TRY
    box.Value = "bad"
CATCH err
    PRINT err.Message
END TRY
```

## Current alpha limitations

* Type checks are runtime checks, not static compilation checks.
* Generic classes are not implemented.
* Multiple inheritance is not implemented.
* Interfaces currently describe methods, not fields.
* Constructors do not return values.
* Class instances are object-backed values marked with runtime class metadata.

These limitations are intentional for the current alpha and keep the object
system simple enough to stabilize.
