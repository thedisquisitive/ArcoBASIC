PRINT "HELLO"

FOR i = 1 TO 3
    PRINT "COUNT " + i
NEXT

LET alive = TRUE

IF alive THEN
    PRINT "READY"
ELSE
    PRINT "STOPPED"
END IF

words = ["alpha", "beta"]
IF words CONTAINS "beta" THEN
    PRINT Upper("helpers ready")
END IF
