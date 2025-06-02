import re

def expand_structs(struct_decl):
    # remove comments and special features
    text = re.sub(r'//.*|/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'#.*', '', text)

    # remove GCC __attribute__ macros
    text = re.sub(r'__attribute__\([^)]+\)\)', '', text)  # remove leading digits


def get_c_structs(c_code, debug=False):

    def show(msg):
        if debug:
            print(msg)

    structs = []
    text = c_code

    show([text])

    beginning_struct = re.compile('typedef\s+struct[^\{]+\{|struct[^\{]+\{')
    findings = re.findall(beginning_struct, text)
    while len(findings) > 0:
        # find the beginning of any struct in code (until the open brackets)
        begin = findings[0]
        start = text.find(begin)

        # find the correct closing bracket (if any brackets open, disregard then)
        idx = start + len(begin)
        missing_closing_brackets = 1
        while missing_closing_brackets > 0:
            if text[idx] == '{':
                missing_closing_brackets += 1
            elif text[idx] == '}':
                missing_closing_brackets -= 1
            show(f"search bracket at: {idx} {text[idx]} missing {missing_closing_brackets}")
            idx += 1
        show(f"block segment: {[text[start:idx]]}")

        # now search for the semicolon
        while text[idx] != ';':
            show(f"search semicolon at: {idx} {text[idx]}")
            idx += 1
        end = idx +1 # to include the semicolon

        show(f"block struct.: {[text[start:end]]}")
        block = text[start:end]

        # now we have the block, we can remove from the text to continue parsing
        text = text.replace(block, '')
        findings = re.findall(beginning_struct, text)

        # if the struct started with typedef, we need to find the name first
        # otherwise, it can still declare variables before the ;
        if block.startswith('typedef'):
            struct = block.strip()
        else:
            # erase everything after the last curly bracket
            variables = block.split('}')[-1]
            struct = block.replace(variables, ';').strip()
        show("struct......: {[struct]}")
        structs.append(struct)
    return structs


if __name__ == "__main__":
    with open('testcases.c', 'r') as file:
        c_code = file.read()

    for s in get_c_structs(c_code):
        print(s)
