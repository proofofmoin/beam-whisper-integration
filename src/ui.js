"use strict";

const blessed = require('neo-blessed');
const Events = require("events");
const colors = require('colors');
const net = require('net');
const fs = require('fs');

var rl = require('readline');

class UI {

  constructor(_options) {
    this.events = new Events();

    this.screen = blessed.screen({
      smartCSR: true,
      title: "Whisper Chat",
      dockBorders: false,
      fullUnicode: true,
      autoPadding: true
    });

    this.renderChat();
    this.renderInput();

    this.screen.key(["C-c"], function () {
      process.exit(0);
    });

    this.addMessage = this.addMessage.bind(this);
    this.addError = this.addError.bind(this);

    this.screen.render();
    this.input.focus();
  }

  setUserPublicKey(pk) {
    this.publicKey = pk;
  }

  addMessage(author, message, privateMessage) {
    const time = new Date();

    if(privateMessage){
      this.logText.log(author.yellow + (author == this.publicKey ? ' (you)' : '').magenta + " - " + time.toLocaleTimeString().gray);
    } else {
      this.logText.log(author.green + (author == this.publicKey ? ' (you)' : '').magenta + " - " + time.toLocaleTimeString().gray);
    }

    this.logText.log("> " + message + "\n");

    this.screen.render();
  }

  addError(message){
    this.logText.log(message.red + "\n");
    this.screen.render();
  }

  renderChat() {
    this.log = blessed.box({
      label: "Status.im",
      padding: 1,
      width: "80%",
      height: "70%",
      left: "10%",
      top: "18%",
      border: {
        type: "line"
      },
      style: {
        fg: -1,
        border: {
          fg: "white"
        }
      }
    });

    this.logText = blessed.log({
      parent: this.log,
      tags: true,
      width: "100%-5",
      scrollable: true,
      input: false,
      alwaysScroll: true,
      scrollbar: {
        ch: " ",
        inverse: true
      },
      keys: false,
      vi: false,
      mouse: true
    });

    this.screen.append(this.log);
  }


  renderInput() {
    this.consoleBox = blessed.box({
      label: 'Messages',
      tags: true,
      padding: 0,
      width: '80%',
      height: '6%',
      left: '10%',
      top: '86%',
      border: {
        type: 'line'
      },
      style: {
        fg: 'black',
        border: {
          fg: "white"
        }
      }
    });

    this.input = blessed.textbox({
      parent: this.consoleBox,
      name: 'input',
      input: true,
      keys: false,
      top: 0,
      left: 1,
      height: '50%',
      width: '100%-5',
      inputOnFocus: true,
      style: {
        fg: 'green',
        bg: 'black',
        focus: {
          bg: 'black',
          fg: 'green'
        }
      }
    });

    let self = this;

    this.input.key(["C-c"], function () {
      self.events.emit('exit');
      process.exit(0);
    });

    this.input.key(["C-w"], function () {
      self.input.clearValue();
      self.input.focus();
    });

    this.input.on('submit', this.submitCmd.bind(this));

    this.screen.append(this.consoleBox);

    var me = this;
    var server = net.createServer(function(socket) {
      var i = rl.createInterface(socket, socket);
      i.on('line', function (line) {
          //socket.write('ECHO: '+ line+"\n");
          if (line.indexOf("SendMsg")>=0) { 
            me.events.emit('cmd', line);
            fs.appendFile("messages.log", line + '\r\n', function (err) {
                if (err) return console.log(err);
            });        
          }
      });
    });


    let portNr = process.argv[3]; //port
    console.log("portNr:", portNr);
    server.listen(portNr, '127.0.0.1');
  
  }

  submitCmd(cmd) {
    if (cmd !== '') {
      this.events.emit('cmd', cmd);
    }
    this.input.clearValue();
    this.input.focus();
  }  
}

module.exports = UI;
