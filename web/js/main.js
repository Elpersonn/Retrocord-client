/*
Retrocord JS client

MADE BY ELPERSON 2021
*/
const nil = null
let servercon;
let token
let logged_in = false // will probably use later idk
let servers = []
let channels = {}
let messages = []
function sanitize(str) {
        const map = {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#x27;',
            "/": '&#x2F;',
        };
        const reg = /[&<>"'/]/ig;
        return str.toString().replace(reg, (match)=>(map[match]));
      
}
function writeMSG(obj) {
    const elem = document.createElement('p')
    elem.classList.add('text')
    elem.classList.add('retrocordcom')
    elem.setAttribute("data-uuid", obj.uuid)
    elem.setAttribute("data-sent", obj.time)
    elem.innerHTML = sanitize(obj.author + ': ' + obj.content)
    document.getElementById("chat").appendChild(elem)

}
function writeMSGraw(content) {
    const elem = document.createElement('p')
    elem.classList.add('text')
    elem.classList.add('retrocordcom')
    elem.innerHTML = sanitize(content)
    document.getElementById("chat").appendChild(elem)

}


function onmsg(evnt) {
    const parsed = JSON.parse(evnt.data)
    //writeMSGraw(evnt.data)
    switch (parsed.action) {
        case 1:
            //writeMSGraw(parsed.payload.MOTD)
            servercon.send(JSON.stringify({action: 2, token: token, payload: {}}))
            break;
        case 2:
            let optns = document.getElementById("servers")
            servers = parsed.payload
            //writeMSGraw(servers.length)
            for (let i=0; i < servers.length; i++) {
                const elem = document.createElement('option')
                elem.innerHTML = servers[i].name
                elem.value = servers[i].uuid
                optns.appendChild(elem)
            }
            //writeMSGraw(JSON.stringify({action: 3, token: token, payload: {servers: servers}}))
            servercon.send(JSON.stringify({action: 3, token: token, payload: {servers: servers}}))

            break;
        case 3:
            channels = parsed.payload
            break;
        case 101:
            messages.push(parsed.payload)
            if (document.getElementById('channels').value == parsed.payload.channel) {
                writeMSG(parsed.payload)
            }    
    }
}
function onopen(evnt) {
    logged_in = true
    document.getElementById("loadstat").innerHTML = "Connected!"

    window.setTimeout("document.getElementById('loading').style.visibility = 'hidden'", 1500)
    document.getElementById('center').style.visibility = 'hidden'
    document.getElementById('retrocord').style.visibility = 'visible'
    document.getElementById("login").disabled = false



}
function onclose(evnt) {
    logged_in = false
    document.getElementById("retrocord").style.visibility = 'hidden'
    document.getElementById("center").style.visibility = 'visible'
    document.getElementById("servers").innerHTML = ''
    document.getElementById("channels").innerHTML = ''
    servers = []
    channels = {}
    messages = []
}
function onerr(evnt) {

}


async function connect(usrn, passwd) {
    let res = await fetch("http://retrocord.elperson.pro:5000/login", {
        method: 'POST',
        mode: 'cors',
        cache: 'no-cache',
        credentials: 'include',
        headers: {
            'authorization': 'Basic ' + window.btoa(usrn+":"+passwd),
            'Content-Type': 'text/plain'
        }
    }).catch(() => {
        return false
    })
        console.log('res'+res.ok)
        if (res.ok) {
            return await res.text()
        }else {
            
            return false
        }
   
    

}
/*function connectWS(ticket) {
    const servercon = new WebSocket("ws://localhost:5000?access_token="+ticket)

}*/
document.getElementById("login").onclick = async () => {
    document.getElementById("login").disabled = true
    document.getElementById("loading").style.visibility = "visible"
    document.getElementById("loadstat").innerHTML = "Logging in..."
    let usrn = document.getElementById("usrn").value
    let passwd = document.getElementById("passwd").value
    let res = await connect(usrn, passwd)
    console.log('theres'+res)
    if (res) {
        document.getElementById("loadstat").innerHTML = "Connecting to WS server..."
        servercon = new WebSocket("ws://retrocord.elperson.pro:5000?access_token="+res)
        servercon.onclose = onclose
        servercon.onopen = onopen
        servercon.onmessage = onmsg
        token = res
    }else {
        document.getElementById("loadstat").innerHTML = "Login failed!"
        
        window.setTimeout("document.getElementById('loading').style.visibility = 'hidden'; document.getElementById('login').disabled = false;", 1500)

    }
}

document.getElementById("msgsub").onclick = () => {
    let val = document.getElementById("msgin").value
    console.log(val.substr(0, 4))
    if (val.substr(0, 4) == "CMD:") { 
       val = val.replace("CMD: ", "")
       switch (val) { // todo make a better command system (maybe call functions with string names??)
           case 'LOGOFF':
                servercon.close(1000)
                document.getElementById("retrocord").style.visibility = 'hidden'
                document.getElementById("center").style.visibility = 'visible'
                document.getElementById("servers").innerHTML = ''
                document.getElementById("channels").innerHTML = ''
                servers = []
                channels = {}
                messages = []
                break;
            case 'CLRCACHE':
                messages = []
                break;
       }
    }else {
        servercon.send(JSON.stringify({action: 101, token: token, payload: {content: document.getElementById('msgin').value.trim(), channel: document.getElementById('channels').value, server: document.getElementById('servers').value}}))
    }
    document.getElementById("msgin").value = '';

}


document.getElementById("channels").onchange = () => {
    const optn = document.getElementById("channels").value
    document.getElementById("chat").innerHTML = ''
    messages.forEach(elem => {
        if (elem.channel == optn) {
            writeMSG(elem)
            //writeMSG(document.getElementById("channels").value == elem.channel)

        }
    });
}

document.getElementById("servers").onchange = () => {
    document.getElementById("chat").innerHTML = ''
    const optn = document.getElementById("servers").value
    

    //writeMSG(optn)
    document.getElementById("channels").innerHTML = '';
    const elemt = document.createElement('option')
    elemt.innerHTML = "select chnl"
    document.getElementById("channels").appendChild(elemt)
    for (let i=0; i<channels[optn].length; i++) {
        const elem = document.createElement('option')
        elem.innerHTML = channels[optn][i].name
        elem.value = channels[optn][i].uuid
        document.getElementById("channels").appendChild(elem)
    }
}

//webcon.setRequestHeader("Access-Control-Allow-Origin", "*")
//webcon.withCredentials = true