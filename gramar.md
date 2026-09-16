# SQL Grammar

```ebnf

Program ::= StmList

StmList ::= Stm [([;] Stm)*]

Stm ::= CreateTableStm
      | CreateIndexStm
      | DropTableStm
      | SelectStm
      | InsertStm
      | DeleteStm
      | UpdateStm
      | TxStm

CreateTableStm ::= create table id (ColDecList) [FromFile]
                 | create table id FromFile

ColDecList ::= ColDec (, ColDec)*
ColDec ::= id Type (Constraint)*

Type ::= int
       | varchar(Num)

Constraint ::= primary key
             | unique
             | index IndexType

FromFile ::= from file Str [using index IndexType (id)]

IndexType ::= sequential
            | heap
            | btree
            | hash

CreateIndexStm ::= create index id on id using IndexType (id)

DropTableStm ::= drop table id

SelectStm ::= select SelList from Source [Where] [GroupBy] [OrderBy] [Limit]

SelList ::= *
          | SelItem (, SelItem)*
SelItem ::= CExp [as id]

Source ::= TableRef (Join)*
TableRef ::= id [[as] id]
Join ::= join TableRef on CExp

Where ::= where CExp

GroupBy ::= group by ExpList [having CExp]

OrderBy ::= order by OrderItem (, OrderItem)*
OrderItem ::= CExp [Direction]
Direction ::= asc
            | desc

Limit ::= limit Num [offset Num]

InsertStm ::= insert into id [(id (, id)*)] values Row (, Row)*
Row ::= (ExpList)

DeleteStm ::= delete from id [Where]

UpdateStm ::= update id set Assign (, Assign)* [Where]
Assign ::= id = CExp

TxStm ::= begin [transaction]
        | commit
        | end transaction
        | rollback

ExpList ::= CExp (, CExp)*

CExp ::= OrExp

OrExp ::= AndExp (or AndExp)*
AndExp ::= NotExp (and NotExp)*
NotExp ::= [not] CompExp
CompExp ::= Exp [CompTail]
Exp ::= Term (( + | - ) Term)*
Term ::= Factor (( * | % ) Factor)*

CompTail ::= CompOp Exp
           | [not] between Exp and Exp
           | [not] in (ExpList)
           | [not] like Exp

CompOp ::= =
         | <>
         | !=
         | <
         | <=
         | >
         | >=

PrefixOp ::= -

Factor ::= Column
         | Num
         | Str
         | (CExp)
         | PrefixOp Factor
         | id([ArgList])
         | id(*)

ArgList ::= CExp (, CExp)*

Column ::= id [. id]

Reserved ::= inner
           | left
           | right
           | outer
           | full

Str ::= 'Char*'

Num ::= Digit+

Digit ::= 0
        | 1
        | 2
        | 3
        | 4
        | 5
        | 6
        | 7
        | 8
        | 9
```